/**
 * Fixed-capacity ring of parallel numeric columns. Memory is allocated once,
 * so the live chart never grows no matter how long the panel stays open.
 */
export class SeriesRing<K extends string> {
  readonly capacity: number
  private cols: Record<K, Float64Array>
  private head = 0
  private len = 0

  constructor(keys: readonly K[], capacity: number) {
    this.capacity = capacity
    this.cols = Object.fromEntries(keys.map((k) => [k, new Float64Array(capacity)])) as Record<K, Float64Array>
  }

  get length() {
    return this.len
  }

  push(row: Record<K, number>) {
    for (const k in this.cols) this.cols[k][this.head] = row[k]
    this.head = (this.head + 1) % this.capacity
    if (this.len < this.capacity) this.len++
  }

  clear() {
    this.head = 0
    this.len = 0
  }

  /** Oldest-to-newest copy of one column (NaN kept, so uPlot shows gaps). */
  ordered(key: K, out?: Float64Array): Float64Array {
    const src = this.cols[key]
    const dst = out && out.length === this.len ? out : new Float64Array(this.len)
    const start = (this.head - this.len + this.capacity) % this.capacity
    const first = Math.min(this.len, this.capacity - start)
    dst.set(src.subarray(start, start + first), 0)
    if (first < this.len) dst.set(src.subarray(0, this.len - first), first)
    return dst
  }

  last(key: K): number {
    if (!this.len) return NaN
    return this.cols[key][(this.head - 1 + this.capacity) % this.capacity]
  }
}
