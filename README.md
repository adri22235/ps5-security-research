# PlayStation 5 (Prospero OS) Security Research & Architecture Dossier

[![ISO/IEC 29147](https://img.shields.io/badge/Standard-ISO%2FIEC%2029147%20CVD-blue.svg)](https://www.iso.org/standard/45170.html)
[![Target](https://img.shields.io/badge/Target-PS5%20Prospero%20OS-purple.svg)](https://github.com/adri22235/ps5-security-research)
[![Author](https://img.shields.io/badge/Researcher-%40adri22235-brightgreen.svg)](https://github.com/adri22235)

Independent systems security research repository focusing on **PlayStation 5 (Prospero OS)** kernel attack surfaces, FreeBSD upstream vulnerability mapping, and Ring 0 to Ring -1 (Hypervisor) execution flow.

**Lead Research Entity:** García Casado Security Research (García Casado Cyber Lab)  
**Lead Systems Security Researcher:** Adrián García Casado (`@adri22235`)  
**Legal Framework:** ISO/IEC 29147 (Coordinated Vulnerability Disclosure) & ISO/IEC 30111  

---

## 1. Executive Summary & Security Layer Topology

The PlayStation 5 operates on **Prospero OS**, an advanced evolution of the Orbis OS kernel derived from modern FreeBSD revisions and tailored for AMD Zen 2 (x86-64) microarchitecture. 

The security architecture is enforced across three distinct privilege rings:

```mermaid
flowchart TD
    subgraph "Ring 3 — Userland Isolation"
        A["WebKit / Browser Engine\n(JavaScriptCore / WebCore)"]
        B["Application & Game Sandbox\n(Restricted Sockets, Capsicum / Mac Labeling)"]
    end

    subgraph "Ring 0 — Prospero Kernel (FreeBSD Base)"
        C["Process & Credential Management\n(struct proc / struct ucred)"]
        D["FreeBSD Subsystems\n(sysv_sem, sound(4), kern_exec, sys_generic)"]
        E["Memory Manager & Direct Memory\n(UMA Slabs / Physical Allocations)"]
    end

    subgraph "Ring -1 — Hypervisor & Hardware Security"
        F["Prospero Hypervisor (HV / SVM)\n(Nested Page Tables / SLAT Enforcement)"]
        G["AMD Secure Processor (PSP / SAMU)\n(Cryptographic Enclaves & Key Storage)"]
    end

    A -->|Userland Control Flow / ROP| C
    B -->|Local Syscall Primitives| D
    D -->|Kernel R/W & KASLR Defeat| E
    E -->|Kernel Execution (Ring 0)| F
    F -->|Byepervisor Reborn Exploit Chain| G
```

---

## 2. The Core Bottleneck & Hypervisor Coupling

In the PlayStation 5 scene, significant research has focused on **Byepervisor / Byepervisor Reborn**, a hypervisor-level exploit capable of bypassing nested page table (NPT) protections at Ring -1.

However, a strict operational prerequisite exists:
> **The Hypervisor Dependency:** Byepervisor cannot be triggered from unprivileged userland (Ring 3). It strictly requires **prior arbitrary code execution in the Kernel (Ring 0)**.

Our research bridges this gap by cataloging and verifying the upstream FreeBSD kernel vulnerabilities that grant Ring 0 entry, providing the indispensable trigger for Ring -1 research.

---

## 3. FreeBSD-Derived Kernel Vectors for Prospero OS

Because Prospero OS inherits core POSIX and BSD subsystems from FreeBSD, vulnerabilities discovered in upstream FreeBSD directly affect unpatched Prospero revisions:

| Vulnerability | Upstream Advisory | Target Subsystem | Vulnerability Class & PS5 Applicability |
| :--- | :--- | :--- | :--- |
| **CVE-2026-101308** | [FreeBSD-SA-26:71.sound](https://security.freebsd.org/advisories/FreeBSD-SA-26:71.sound.asc) | `sys/dev/sound/pcm/buffer.c` | **Kernel Use-After-Free via `mmap` race:** Dropping channel lock during `sndbuf_resize` with `M_WAITOK` creates an asynchronous TOCTOU race with concurrent `mmap(2)`. Exposes dangling kernel audio buffer. |
| **CVE-2026-58087** | [FreeBSD-SA-26:54.sysvsem](https://security.freebsd.org/advisories/FreeBSD-SA-26:54.sysvsem.asc) | `sys/kern/sysv_sem.c` | **15-Bit Wrap OOB Read/Write:** Integer wrap-around in semaphore sequence counter (`0x8000` / 32,768 cycles) in `semctl` `GETALL`/`SETALL`. Accessible via standard unprivileged Syscall 510. |
| **CVE-2026-7270** | [FreeBSD-SA-26:13.exec](https://lists.freebsd.org/archives/freebsd-security/2026-April/000448.html) | `sys/kern/kern_exec.c` | **Kernel Slab Buffer Overflow:** Operator precedence flaw in `exec_args_adjust_args()` results in an out-of-bounds forward copy of $+1024$ bytes into the kernel slab. |
| **CVE-2026-58098** | [FreeBSD-SA-26:64.sysvsem](https://security.freebsd.org/advisories/FreeBSD-SA-26:64.sysvsem.asc) | `sys/kern/sysv_sem.c` | **Heap OOB Access:** Out-of-bounds array access in `semop(2)` Syscall 511 during parameter validation. |

---

## 4. Firmware Chronology: The 14.00 vs 14.10 Divide

* **September 16, 2026:** Sony deploys **Firmware 14.00** across the PlayStation ecosystem.
* **October 1, 2026:** In response to accelerated scene progress surrounding *Relapse* and *Byepervisor Reborn*, Sony issues emergency maintenance update **Firmware 14.10 for PlayStation 5**.
* **October 15, 2026:** Sony enforces Firmware 14.10 as **strictly mandatory for PlayStation Network (PSN) access**, actively cutting off network connectivity for consoles running earlier revisions to prevent further deployment of Ring -1 bypasses.
* **Research State:** Firmware $\le$ 14.00 represents the primary target window where these kernel vectors remain unmitigated.

---

## 5. Architectural Comparison: Orbis OS vs Prospero OS

| Architectural Feature | PlayStation 4 (Orbis OS) | PlayStation 5 (Prospero OS) | Research Impact |
| :--- | :--- | :--- | :--- |
| **CPU Architecture** | AMD Jaguar (8 cores, x86-64) | AMD Zen 2 (8 cores / 16 threads) | Different cache hierarchy and instruction timing |
| **Kernel Heritage** | FreeBSD 9 base | FreeBSD 11/12+ hybrid base | Subsystems (IPC, Sound, VFS) share upstream flaws |
| **Virtualization Layer**| Flat Ring 0 / Hardware Ring 3 | Hardware SVM Hypervisor (Ring -1) | Ring 0 compromise required before Ring -1 attack |
| **W^X / DEP** | Software enforced | Hardware SMEP / SMAP / Hardware DEP | ROP chains required for initial control-flow hijack |
| **Network Sandbox** | Restricts non-`AF_UNIX` sockets | Restricts raw and non-local sockets | Favors local kernel memory corruption vectors |

---

## 6. Repository Roadmap

* [`PROSPERO_ARCHITECTURE.md`](PROSPERO_ARCHITECTURE.md) — Detailed analysis of AMD Zen 2 SVM and hypervisor page table protections.
* [`KERNEL_VECTORS_PS5.md`](KERNEL_VECTORS_PS5.md) — Source audit of FreeBSD 11/12+ subsystems in Prospero OS.
* [`tests/`](tests/) — Memory safety test benches verifying race windows and slab layout.

---

## Scope & Legal Compliance

All research presented in this repository is strictly conducted under the **ISO/IEC 29147 (Coordinated Vulnerability Disclosure)** and **ISO/IEC 30111** standards. No proprietary Sony Interactive Entertainment firmware binaries, decrypted keys, or unauthorized circumvention tools are hosted.
