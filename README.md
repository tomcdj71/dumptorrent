# Description
DumpTorrent is a non-interactive text mode program which displays BitTorrent .torrent file information, including size, file names, announce[-list], comment, publisher and info_hash. It can also query (scrape) tracker for current downloader count.

It's forked from [original wuyongzheng version](https://sourceforge.net/projects/dumptorrent/)

# Installation

For Windows and Linux you could try the precompiled versions from the [Releases](https://github.com/TheGoblinHero/dumptorrent/releases). Otherwise, compile it yourself. 

## Linux Compilation

```
apt-get install build-essential git
git clone https://github.com/TheGoblinHero/dumptorrent.git
cd dumptorrent
make
```

## ruTorrent Integration

1. Copy dumptorrent binary file somethere (system path is the best place).

2. If needed, edit the configuration file for the dump plugin (rutorrent/plugins/dump/conf.php) and 
update the path to match where you placed the binary. 

3. Restart ruTorrent.
