#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include "support.h"
#include "channel.h"

TSC_SIGNAL_MASK_DECLARE

static inline void __chan_memcpy (void *dst, const void *src, size_t size)
{
    if (dst && src)
        memcpy (dst, src, size);
}

typedef struct quantum {
	bool select;
	channel_t chan;
	thread_t thread;
	uint8_t * itembuf;
	queue_item_t link;
} quantum;

static inline void quantum_init (quantum * q, channel_t chan, thread_t thread, void * buf, bool select)
{
	q -> select = select;
	q -> chan = chan;
	q -> thread = thread;
	q -> itembuf = buf;
	queue_item_init (& q -> link, q);
}

channel_t channel_allocate (int32_t elemsize, int32_t bufsize)
{
	struct channel * chan = TSC_ALLOC (sizeof(struct channel) + elemsize * bufsize);
	assert (chan != NULL);

	chan -> elemsize = elemsize;
	chan -> bufsize = bufsize;	
	chan -> buf = (uint8_t *)(chan + 1);
	chan -> nbuf = 0;
	chan -> recvx = chan -> sendx = 0;

	lock_init(& chan -> lock);

	queue_init (& chan -> recv_que);
	queue_init (& chan -> send_que);

	return chan;
}

void channel_dealloc (channel_t chan)
{
	/* TODO: awaken the sleeping threads */
	lock_fini(& chan -> lock);
	TSC_DEALLOC (chan);
}

static quantum * fetch_quantum (queue_t * queue)
{
	quantum * q = NULL;
	while (q = queue_rem (queue)) {		
		// if this quantum is generated by a select call,
		// only one task can fetch it!
		if (!(q -> select) ||
			TSC_CAS(& q -> thread -> qtag, NULL, q -> chan)) break;
	}

	return q;
}

static int __channel_send (channel_t chan, void * buf, bool block)
{
	thread_t self = thread_self();

	// check if there're any waiting threads ..
	quantum * qp = fetch_quantum (& chan -> recv_que);
	if (qp != NULL) {
		__chan_memcpy (qp -> itembuf, buf, chan -> elemsize);
		vpu_ready (qp -> thread);
		return CHAN_SUCCESS;
	}
	// check if there're any buffer slots ..
	if (chan -> nbuf < chan -> bufsize) {
		uint8_t * p = chan -> buf;
		p += (chan -> elemsize) * (chan -> sendx ++);
		__chan_memcpy (p, buf, chan -> elemsize);
		(chan -> sendx) %= (chan -> bufsize);
		chan -> nbuf ++;
		return CHAN_SUCCESS;
	}
	// block or return CHAN_BUSY ..
	if (block) {
		// the async way ..
			quantum q;
			quantum_init (& q, chan, self, buf, false);
			queue_add (& chan -> send_que, & q . link);
			vpu_suspend (NULL, & chan -> lock, (unlock_hander_t)(lock_release));
			// awaken by a receiver later ..
		return CHAN_AWAKEN;
	} 

	return CHAN_BUSY;
}

static int __channel_recv (channel_t chan, void * buf, bool block)
{
	thread_t self = thread_self ();

	// check if there're any senders pending .
	quantum * qp = fetch_quantum (& chan -> send_que);
	if (qp != NULL) {
		__chan_memcpy (buf, qp -> itembuf, chan -> elemsize);
		vpu_ready (qp -> thread);
		return CHAN_SUCCESS;
	}
	// check if there're any empty slots ..
	if (chan -> nbuf > 0) {
		uint8_t * p = chan -> buf;
		p += (chan -> elemsize) * (chan -> recvx ++);
		__chan_memcpy (buf, p, chan -> elemsize);
		(chan -> recvx) %= (chan -> bufsize);
		chan -> nbuf --;
		return CHAN_SUCCESS;
	}
	// block or return CHAN_BUSY
	if (block) {
		// async way ..
			quantum q;
			quantum_init (& q, chan, self, buf, false);
			queue_add (& chan -> recv_que, & q . link);
			vpu_suspend (NULL, & chan -> lock, (unlock_hander_t)(lock_release));
			// awaken by a sender later ..
		return CHAN_AWAKEN;
	}

	return CHAN_BUSY;
}

/* -- the public APIs for channel -- */
int _channel_send (channel_t chan, void * buf, bool block)
{
	TSC_SIGNAL_MASK();

	int ret;
	lock_acquire (& chan -> lock);
	ret = __channel_send (chan, buf, block);
	if (ret != CHAN_AWAKEN) // !!
		lock_release (& chan -> lock);

	TSC_SIGNAL_UNMASK();
	return ret;
}

int _channel_recv (channel_t chan, void * buf, bool block)
{
	TSC_SIGNAL_MASK();

	int ret;
	lock_acquire (& chan -> lock);
	ret = __channel_recv (chan, buf, block);
	if (ret != CHAN_AWAKEN) // !! 
		lock_release (& chan -> lock);

	TSC_SIGNAL_UNMASK();
	return ret;
}

#if defined(ENABLE_CHANNEL_SELECT)
enum {
	CHAN_SEND,
	CHAN_RECV,
};

typedef struct elem {
	int type;
	channel_t chan;
	void * buf;
	queue_item_t link;
} * elem_t;

static inline elem_t elem_alloc (int type, channel_t chan, void * buf)
{
	elem_t elem = TSC_ALLOC(sizeof (struct elem));
	assert (elem != NULL);

	elem -> type = type;
	elem -> chan = chan;
	elem -> buf = buf;
	queue_item_init (& elem -> link, elem);
	return elem;
}

/* -- the public APIs for channel select -- */
chan_set_t chan_set_allocate (void)
{
	chan_set_t set = TSC_ALLOC(sizeof(struct chan_set));
	assert (set != NULL);
	lock_chain_init(& set -> locks);
	queue_init (& set -> sel_que);

	return set;
}

void chan_set_dealloc (chan_set_t set)
{
	lock_chain_fini (& set -> locks);
	TSC_DEALLOC(set);
}

void chan_set_send (chan_set_t set, channel_t chan, void * buf)
{
	assert (set != NULL && chan != NULL);
	chan -> select = true;
	lock_chain_add (& set -> locks, & chan -> lock);
	elem_t e = elem_alloc (CHAN_SEND, chan, buf);
	queue_add (& set -> sel_que, & e -> link);
}

void chan_set_recv (chan_set_t set, channel_t chan, void * buf)
{
	assert (set != NULL && chan != NULL);
	chan -> select = true;
	lock_chain_add (& set -> locks, & chan -> lock);
	elem_t e = elem_alloc (CHAN_RECV, chan, buf);
	queue_add (& set -> sel_que, & e -> link);
}

int _chan_set_select (chan_set_t set, bool block, channel_t * active)
{
	assert (set != NULL);
	TSC_SIGNAL_MASK();

	if (set -> sel_que . status == 0) {
		* active = NULL; 
		return -1;
	}

	int ret;
	thread_t self = thread_self ();
	lock_chain_acquire (& set -> locks);

	queue_item_t * sel = set -> sel_que . head;
	while (sel != NULL) {
		elem_t e = (elem_t)(sel -> owner);

		switch (e -> type) {
			case CHAN_SEND:
				ret = __channel_send (e -> chan, e -> buf, false);
				break;
			case CHAN_RECV:
				ret = __channel_recv (e -> chan, e -> buf, false);
		}
		if (ret == CHAN_SUCCESS) {
			* active = e -> chan; 
			goto __leave_select;
		}

		sel = sel -> next;
	}

	// got here means we can not get any active chan event
	// so block or return CHAN_BUSY ..
	if (block) {
		// TODO : add quantums ..
		self -> qtag = NULL;
		sel = set -> sel_que . head;
		quantum * qarray = TSC_ALLOC((set -> sel_que . status) * sizeof (quantum));
		quantum * pq = qarray;

		while (sel != NULL) {
			elem_t e = (elem_t)(sel -> owner);
			quantum_init (pq, e -> chan, self, e -> buf, true);
			switch (e -> type) {
			case CHAN_SEND:
				queue_add (& e -> chan -> send_que, & pq -> link);
				break;
			case CHAN_RECV:
				queue_add (& e -> chan -> recv_que, & pq -> link);
				break;
			}
			sel = sel -> next;
			pq ++;
		}

		vpu_suspend (NULL, & set -> locks, lock_chain_release);
		lock_chain_acquire (& set -> locks);
		
		// dequeue all unactive chans ..
		sel = set -> sel_que . head;
		pq = qarray;

		while (sel != NULL) {
			elem_t e = (elem_t)(sel -> owner);
			queue_t * que = & (e -> chan -> send_que);
			if (e -> type == CHAN_RECV)
				que = & (e -> chan -> recv_que);

			if (queue_lookup (que, general_inspector, pq)) {
				queue_extract (que, & pq -> link);				
			}
			sel = sel -> next;
			pq ++;
		}

		// relase the quantums , safe ?
		TSC_DEALLOC (qarray);

		* active = (channel_t)(self -> qtag);
		self -> qtag = NULL;
		ret = CHAN_SUCCESS;
	}

__leave_select:
	lock_chain_release (& set -> locks);
	TSC_SIGNAL_UNMASK();
	return ret;
}

#endif // ENABLE_CHANNEL_SELECT
