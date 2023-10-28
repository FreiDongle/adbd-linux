# Limitation
Openssl version < 1.1, must be 1.0, as the 1.1 has broken API

# Info
Base on:https://github.com/hadess/adbd

Modify for Yocto building and common Linux based OS.

# Compiling in normal Linux OS
```bash
make -j $(expr $(nproc) - 1)
```

# Compiling with cross toolchains

Build on Debian 11 with aarch64 toolchain is shown as example.

In any Linux or WSL shell, install Docker and [Distrobox](https://github.com/89luca89/distrobox). Then run

```bash
distrobox ephemeral --image debian:11 --additional-packages 'build-essential g++-aarch64-linux-gnu libssl-dev:arm64 libcap-dev:arm64 libglib2.0-dev:arm64' --name debian-aarch64 --pre-init-hooks 'dpkg --add-architecture arm64'
```

Environment setup will succeed with message `Container Setup Complete!`, then open a sub-shell in current working directory with selected distro, which in our case is Debian 11.

In the sub-shell, build with env variable `CROSS_PREFIX`

```bash
X=aarch64-linux-gnu
CROSS_PREFIX=$X- make -j $(expr $(nproc) - 1)
```

or if you prefer to set tool names individually

```bash
RANLIB=$X-ranlib AR=$X-ar CXX=$X-g++ CC=$X-gcc PKG_CONFIG=$X-pkg-config make
```