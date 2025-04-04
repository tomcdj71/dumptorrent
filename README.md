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
git clone https://github.com/TheGoblinHero/dumptorrent.git
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

---

##  ruTorrent Integration

To integrate `dumptorrent` with **ruTorrent's `Dump` plugin**:

 1) Edit the plugin config: Open `rutorrent/plugins/dump/conf.php` and update the path to the `dumptorrent` binary if needed.
 2) Restart ruTorrent or reload the plugin.
