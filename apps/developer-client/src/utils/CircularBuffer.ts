/**
 * Circular buffer for fixed-size rolling window of data.
 * O(1) insert, fixed memory, no array shifting.
 */
export class CircularBuffer<T> {
  private buffer: T[];
  private head: number = 0;
  private size: number = 0;
  private readonly capacity: number;

  constructor(capacity: number) {
    this.capacity = capacity;
    this.buffer = new Array(capacity);
  }

  /**
   * Add item to buffer. If full, overwrites oldest item.
   */
  push(item: T): void {
    this.buffer[this.head] = item;
    this.head = (this.head + 1) % this.capacity;
    if (this.size < this.capacity) {
      this.size++;
    }
  }

  /**
   * Get all items in chronological order (oldest to newest).
   */
  getAll(): T[] {
    if (this.size === 0) return [];

    const result: T[] = [];
    const start = this.size < this.capacity ? 0 : this.head;

    for (let i = 0; i < this.size; i++) {
      const index = (start + i) % this.capacity;
      result.push(this.buffer[index]);
    }

    return result;
  }

  /**
   * Get most recent item, or undefined if empty.
   */
  getLatest(): T | undefined {
    if (this.size === 0) return undefined;
    const lastIndex = (this.head - 1 + this.capacity) % this.capacity;
    return this.buffer[lastIndex];
  }

  /**
   * Clear all items.
   */
  clear(): void {
    this.head = 0;
    this.size = 0;
  }

  /**
   * Current number of items in buffer.
   */
  getSize(): number {
    return this.size;
  }

  /**
   * Maximum capacity of buffer.
   */
  getCapacity(): number {
    return this.capacity;
  }

  /**
   * Check if buffer is empty.
   */
  isEmpty(): boolean {
    return this.size === 0;
  }

  /**
   * Check if buffer is full.
   */
  isFull(): boolean {
    return this.size === this.capacity;
  }
}
