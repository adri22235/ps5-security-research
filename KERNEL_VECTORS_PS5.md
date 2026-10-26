# FreeBSD Kernel Vectors Mapped to Prospero OS (PS5)

**Lead Researcher:** Adrián García Casado (`@adri22235`)  
**Research Entity:** García Casado Security Research  
**Standard:** ISO/IEC 29147 (Coordinated Vulnerability Disclosure)  

---

## 1. Vulnerability Analysis: CVE-2026-101308 (Audio `sound(4)` TOCTOU UAF)

* **Advisory Reference:** [FreeBSD-SA-26:71.sound](https://security.freebsd.org/advisories/FreeBSD-SA-26:71.sound.asc)
* **Subsystem:** `sys/dev/sound/pcm/buffer.c` / `channel.c`
* **Vulnerability Class:** Race Condition / Use-After-Free (UAF)
* **Date Disclosed:** October 14, 2026

### Root Cause Analysis:
In the FreeBSD sound driver implementation, `chn_resizebuf()` checks that the audio channel buffer is not currently memory-mapped via `CHN_F_MMAP`. To allocate replacement memory, the routine drops the channel mutex:

```c
// Upstream FreeBSD vulnerable pattern:
CHN_UNLOCK(c);
new_buf = malloc(new_size, M_DEVBUF, M_WAITOK); // Lock dropped!
CHN_LOCK(c);
// BUG: Missing check: if (c->flags & CHN_F_MMAP) { free(new_buf); return EBUSY; }
sndbuf_free(c->buf); // Frees old buffer while mapped concurrently!
c->buf = new_buf;
```

During this unlocked window, an unprivileged userland thread invokes `mmap(2)` on `/dev/dsp*` or the proprietary audio device node. The kernel maps the buffer to user space. When the resize routine re-acquires the lock, it frees the original buffer without revalidating `CHN_F_MMAP`, leaving the user space process holding a live pointer to freed kernel memory.

### Prospero OS Impact:
Prospero OS utilizes a FreeBSD-derived audio driver subsystem for multi-channel PCM streaming. Because the patch was published in mid-October 2026, Firmware $\le$ 14.00 predates this mitigation.

---

## 2. Vulnerability Analysis: CVE-2026-58087 (SysV IPC `semctl` 15-Bit Wrap)

* **Advisory Reference:** [FreeBSD-SA-26:54.sysvsem](https://security.freebsd.org/advisories/FreeBSD-SA-26:54.sysvsem.asc)
* **Subsystem:** `sys/kern/sysv_sem.c`
* **Vulnerability Class:** Integer Wrap-Around / Out-of-Bounds Read & Write
* **Date Disclosed:** July 29, 2026

### Root Cause Analysis:
The System V semaphore subsystem tracks active sets using a 15-bit sequence counter:

$$\text{Sequence Limit} = 0x8000 = 32{,}768\text{ cycles}$$

In commands `GETALL` and `SETALL`, `semctl()` records the semaphore count, drops the lock to allocate a temporary transfer buffer, and re-acquires the lock. By rapidly creating and destroying semaphore sets in user space, an attacker cycles the 15-bit counter past $0x8000$, causing a collision with a different semaphore set possessing an altered allocation size.

### Post-Exploitation Capability:
* **OOB Read (`GETALL`):** Reads past the allocated buffer into adjacent kernel slab objects, leaking kernel function pointers to defeat KASLR.
* **OOB Write (`SETALL`):** Overwrites adjacent kernel memory objects to hijack function pointers or corrupt credentials.

---

## 3. Vulnerability Analysis: CVE-2026-7270 (Kernel Slab Buffer Overflow in `execve`)

* **Advisory Reference:** [FreeBSD-SA-26:13.exec](https://lists.freebsd.org/archives/freebsd-security/2026-April/000448.html)
* **Subsystem:** `sys/kern/kern_exec.c`
* **Vulnerability Class:** Operator Precedence Flaw / Heap Buffer Overflow
* **Date Disclosed:** April 2026

### Root Cause Analysis:
In `exec_args_adjust_args()`, calculation of argument shift length contains an operator precedence flaw:

$$\text{Calculated Length} = \text{endp} - \text{begin\_argv} + \text{consume}$$

instead of subtracting the consumed length of the interpreter name. This error produces an arithmetic forward shift of $+1024$ bytes ($2 \times \text{consume}$), writing past the boundaries of the `exec_map` slab slice and into adjacent kernel structures.
