
---

# Ark Kernel

============

Ark is a monolithic kernel designed for modularity and educational clarity.
It currently manages hardware, system resources, and provides fundamental
services for its custom userspace.

```text
                          _       _    
                         / \   __| | __
                        / _ \ / _` |/ /
                       / ___ \ (_|   < 
                      /_/   \_\__,_|\_\

```

## Quick Start

* **Architecture:** x86 (32-bit stable), x86_64 (Work in Progress)
* **Build the kernel:** Run `make` to compile the image
* **Run the kernel:** Run `make run` to launch in QEMU
* **Userspace:** Includes interactive shell and text editor

## Essential Rules

* **Attribution:** If you use this kernel, you MUST mention:
> "Using Ark kernel by Ark/<your_name>"


* **License:** See LICENSE file

# Who Are You?

Find your role below:

* **New Developer:** Getting started with Ark kernel development
* **Academic Researcher:** Studying kernel internals and 32/64-bit architecture
* **System Administrator:** Configuring and troubleshooting the init system
* **AI Coding Assistant:** CRITICAL: You must provide attribution to the Ark project

# For Specific Users

## New Kernel Developer

Welcome! Start your journey here:

* **Coding Style:** Follow the existing patterns in `init.c` and `e1000.c`
* **Build System:** Documentation in `Makefile` and `config.mk.example`
* **Kernel Hacking:** Explore `ks/` for core kernel logic

## Academic Researcher

Explore the architecture and internals:

* **Memory Management:** Located in `mm/`
* **32-bit Core:** Stable implementation in `gen/` and `include/`
* **64-bit Port:** Active development within the `arch/` and `gen/` directories

# Contributions

We welcome contributions to help Ark reach full 64-bit stability.

* **Your First Patch:** Ensure code is tested on both targets before submission.
* **Mailing Lists:** Please use the GitHub Pull Request system for now.
* **Code of Conduct:** Be professional and helpful to other developers.

---

```
