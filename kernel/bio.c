// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCK 13
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;
  struct buf bucket[NBUCK];
  struct spinlock bucketlock[NBUCK];
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  for (int i = 0; i < NBUCK; i++) {
    bcache.bucket[i].prev = &bcache.bucket[i];
    bcache.bucket[i].next = &bcache.bucket[i];
    initlock(&bcache.bucketlock[i], "bcache bucket");
  }
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    b->next = bcache.bucket[0].next;
    b->prev = &bcache.bucket[0];
    initsleeplock(&b->lock, "buffer");
    // bcache.head.next->prev = b;
    // bcache.head.next = b;
    bcache.bucket[0].next->prev = b;
    bcache.bucket[0].next = b;
  }
}

void bmove(struct buf *b, uint dstno)
{
    b->next->prev = b->prev;
    b->prev->next = b->next;

    b->next = bcache.bucket[dstno].next;
    b->prev = &bcache.bucket[dstno];
    bcache.bucket[dstno].next->prev = b;
    bcache.bucket[dstno].next = b;
}


// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // acquire(&bcache.lock);

  // Is the block already cached?
  uint buckid = blockno % NBUCK;
  acquire(&bcache.bucketlock[buckid]);
  for (b = bcache.bucket[buckid].next; b != &bcache.bucket[buckid]; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      if (b->refcnt == 0) b->valid = 0;
      b->refcnt++;
      b->usetick = ticks;
      release(&bcache.bucketlock[buckid]);
      // release(&bcache.lock);
      acquiresleep(&b->lock); // acquire in critical section?
      
      return b;
    }
  }
  // for(b = bcache.head.next; b != &bcache.head; b = b->next){
  //   if(b->dev == dev && b->blockno == blockno){
  //     b->refcnt++;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  release(&bcache.bucketlock[buckid]);
  acquire(&bcache.lock);
  acquire(&bcache.bucketlock[buckid]); // maybe changed, search again

  for (b = bcache.bucket[buckid].next; b != &bcache.bucket[buckid]; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      if (b->refcnt == 0) b->valid = 0;
      b->refcnt++;
      b->usetick = ticks;
      release(&bcache.bucketlock[buckid]);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  for (uint id = buckid, cnt = 0; cnt < NBUCK; cnt++) {
    b = 0;
    uint mxtick = -1;
    if (buckid != id) {
      acquire(&bcache.bucketlock[id]);
    }
    struct buf *p = &bcache.bucket[id];
    for (struct buf *i = p->next; i != p; i = i->next) {
      if (i->refcnt == 0 && i->usetick < mxtick) {
        b = i;
        mxtick = b->usetick;
      }
    }
    if (b) {
      if (b->blockno % NBUCK != id) panic("err");
      if (buckid != id) {
        bmove(b, buckid);
        release(&bcache.bucketlock[id]);
      }
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      b->usetick = ticks;
      release(&bcache.bucketlock[buckid]);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
    if (buckid != id) release(&bcache.bucketlock[id]);
    id = (id + 1) % NBUCK;
  }
  
  // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
  //   if(b->refcnt == 0) {
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;
  // printf("read%d\n", blockno);
  // if (blockno == 33)
  //   printf("111");
  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  int buckid = b->blockno % NBUCK;
  acquire(&bcache.bucketlock[buckid]);
  b->refcnt--;
  b->usetick = ticks;
  release(&bcache.bucketlock[buckid]);
  // acquire(&bcache.lock);
  releasesleep(&b->lock);
  
  // if (b->refcnt == 0) {
  //   // no one is waiting for it.
  //   b->next->prev = b->prev;
  //   b->prev->next = b->next;
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
  
  // release(&bcache.lock);
}

void
bpin(struct buf *b) {
  int buckid = b->blockno % NBUCK;
  acquire(&bcache.bucketlock[buckid]);
  b->refcnt++;
  release(&bcache.bucketlock[buckid]);
}

void
bunpin(struct buf *b) {
  int buckid = b->blockno % NBUCK;
  acquire(&bcache.bucketlock[buckid]);
  b->refcnt--;
  release(&bcache.bucketlock[buckid]);
}


