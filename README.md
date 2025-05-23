# DumpTorrent

**DumpTorrent** is a non-interactive, command-line utility that displays detailed information about `.torrent` files. It extracts metadata such as:

- Torrent name
- Total size
- File list
- Tracker announce URLs and announce-list
- Comment
- Creator information
- `info_hash`

It can also perform **tracker scrape queries** to retrieve the current number of seeders, leechers, and completed downloads.

This project is a maintained fork of [TheGoblinHero's version](https://github.com/TheGoblinHero/dumptorrent), itself originally based on [wuyongzheng's implementation](https://sourceforge.net/projects/dumptorrent/).

---

## Installation

### 🔧 Build from Source (Linux)

Ensure required tools are installed:

```bash
apt-get install build-essential git
```

Then clone and build it:

```bash
git clone https://github.com/tomcdj71/dumptorrent.git
cd dumptorrent
cmake -B build/ -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release -S .
cmake --build build/ --config Release --parallel $(nproc)
```

The resulting `dumptorrent` and `scrapec` binaries will be available in the `build` directory.

To install them system-wide:
```bash
chmod +x build/dumptorrent build/scrapec
sudo mv build/dumptorrent build/scrapec /usr/local/bin
```

> [!NOTE] 
> You can move them to any directory in your `$PATH` instead `/usr/local/bin`.

### Install Precompiled Package (Linux)

Pre-built .deb packages are available on the [Releases page](https://github.com/MediaEase-binaries/dumptorrent-builds/releases):

Download the latest release, then install it:
`dpkg -i dumptorrent*.deb`

This will install the binaries into `/usr/local/bin`


### Install on windows

> **Windows support has been removed.**  
> [Release v1.3.0](https://github.com/tomcdj71/dumptorrent/releases/tag/v1.3.0) includes compiled windows binary. But do not expect newer versions.
> `DumpTorrent` now targets Unix-like systems only (`Linux`). Use `WSL` if needed.

### Create your own .deb Package

You can easily create your own Debian package for DumpTorrent using FPM (Effing Package Management). This method is simpler than traditional Debian packaging as it doesn't require complex control files.

#### Prerequisites

Install FPM and required dependencies:

```bash
sudo apt-get update
sudo apt-get install ruby ruby-dev build-essential
sudo gem install fpm
```

#### Build and Package

1. First, compile DumpTorrent from source:

```bash
# Clone the repository
git clone https://github.com/tomcdj71/dumptorrent.git
cd dumptorrent

# Build the binaries
cmake -B build/ -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release -S .
cmake --build build/ --config Release --parallel $(nproc)
```

2. Create a staging directory structure:

```bash
# Create necessary directories
mkdir -p staging/usr/bin

# Copy binaries to staging area
cp build/dumptorrent build/scrapec staging/usr/bin/

# Make binaries executable
chmod +x staging/usr/bin/dumptorrent staging/usr/bin/scrapec
```

3. Build the .deb package with FPM:

```bash
# Get version from CMakeLists.txt
VERSION=$(grep -oP '(?<=set\(DUMPTORRENT_VERSION ")[^"]*' CMakeLists.txt)

# Create the package
fpm -s dir -t deb -C staging \
  --name dumptorrent \
  --version $VERSION \
  --architecture amd64 \
  --description "DumpTorrent is a command-line utility that displays detailed information about .torrent files" \
  --url "https://github.com/tomcdj71/dumptorrent" \
  --maintainer "Your Name <your.email@example.com>" \
  --license "GPL2" \
  --depends "libc6" \
  --deb-compression xz \
  --deb-priority optional \
  --category net \
  usr/bin
```

This will create a file named `dumptorrent_$VERSION_amd64.deb` in your current directory.

4. Install and test your package:

```bash
sudo dpkg -i dumptorrent_*.deb
dumptorrent --version
```

#### Advanced Options

For additional optimizations, you can strip and compress the binaries before packaging:

```bash
# Strip debug symbols
strip --strip-unneeded staging/usr/bin/dumptorrent staging/usr/bin/scrapec

# Install UPX (optional)
sudo apt-get install upx

# Compress binaries with UPX
upx --best --lzma staging/usr/bin/dumptorrent staging/usr/bin/scrapec
```

These steps follow a similar approach to the CI/CD pipeline used in the GitHub Actions workflow.

---

##  ruTorrent Integration

To integrate `dumptorrent` with **ruTorrent's `Dump` plugin**:

 1) Edit the plugin config: Open `rutorrent/plugins/dump/conf.php` and update the path to the `dumptorrent` binary if needed.
 2) Restart ruTorrent or reload the plugin.

## License

As per the original [project](https://sourceforge.net/projects/dumptorrent/), dumptorrent is licensed with the GPL2 license.
