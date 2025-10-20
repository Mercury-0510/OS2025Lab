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

#define HashNumber 91
#define HASH(blockno) (blockno % HashNumber)

struct {
  struct spinlock lock[HashNumber];
  struct spinlock global_lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[HashNumber];
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.global_lock, "bcache_global_lock");
  // Create linked list of buffers
  for(int i = 0; i < HashNumber; i++) {
    initlock(&bcache.lock[i], "bcache_hash_lock");
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }

  int cnt = 0;

  for(b = bcache.buf; b < bcache.buf+NBUF; b++, cnt++) {
    b->next = bcache.head[HASH(cnt)].next;
    b->prev = &bcache.head[HASH(cnt)];
    initsleeplock(&b->lock, "buffer");
    bcache.head[HASH(cnt)].next->prev = b;
    bcache.head[HASH(cnt)].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock[HASH(blockno)]);

  // Is the block already cached?
  for(b = bcache.head[HASH(blockno)].next; b != &bcache.head[HASH(blockno)]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[HASH(blockno)]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head[HASH(blockno)].prev; b != &bcache.head[HASH(blockno)]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[HASH(blockno)]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 将其他桶的一个空闲buf调到当前桶
  int limit = 290;
  release(&bcache.lock[HASH(blockno)]);
  acquire(&bcache.global_lock);
  for(int i = 0,cnt = 0; cnt < limit; i = (i+1)%HashNumber, cnt++) {
    for(b = bcache.head[i].prev; b != &bcache.head[i]; b = b->prev){
      if(b->refcnt == 0) {
        acquire(&bcache.lock[i]);
        if(i != HASH(blockno)) {
          acquire(&bcache.lock[HASH(blockno)]);
        }
        if(b->refcnt != 0) {
          release(&bcache.lock[i]);
          if(i != HASH(blockno)) {
            release(&bcache.lock[HASH(blockno)]);
          }
          continue;
        }
        b->next->prev = b->prev;
        b->prev->next = b->next;
        b->next = bcache.head[HASH(blockno)].next;
        b->prev = &bcache.head[HASH(blockno)];
        bcache.head[HASH(blockno)].next->prev = b;
        bcache.head[HASH(blockno)].next = b;

        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        release(&bcache.lock[i]);
        if(i != HASH(blockno)) {
          release(&bcache.lock[HASH(blockno)]);
        }
        acquiresleep(&b->lock);
        release(&bcache.global_lock);
        return b;
      }
    }
  }
  release(&bcache.global_lock);
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

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

  releasesleep(&b->lock);

  acquire(&bcache.lock[HASH(b->blockno)]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.拉到队首
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[HASH(b->blockno)].next;
    b->prev = &bcache.head[HASH(b->blockno)];
    bcache.head[HASH(b->blockno)].next->prev = b;
    bcache.head[HASH(b->blockno)].next = b;
  }
  
  release(&bcache.lock[HASH(b->blockno)]);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock[HASH(b->blockno)]);
  b->refcnt++;
  release(&bcache.lock[HASH(b->blockno)]);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock[HASH(b->blockno)]);
  b->refcnt--;
  release(&bcache.lock[HASH(b->blockno)]);
}


