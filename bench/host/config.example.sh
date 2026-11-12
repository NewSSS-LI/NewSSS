# Host PC settings. Copy to config.sh (not committed) and fill in.

FTL=                    # firmware on the board: learnedftl | tpftl | dftl | leaftl | greedyftl
BOARD_ID=               # who ran it, e.g. board-hongyeon (goes into result folder names)

RESULTS_DIR=$HOME/newsss_results   # one folder per run is created here
TOOLS_DIR=$HOME/newsss_tools       # setup_host.sh builds Filebench and db_bench here
MNT=/mnt/ssd                       # mount point for Filebench and RocksDB

# The board must look exactly like this, or nothing runs.
EXPECT_MODEL="Cosmos+ OpenSSD"
EXPECT_SIZE=34359738368            # 32GB mode (-DEXP_LOGICAL_GB=32)
EXPECT_PCI=                        # optional, e.g. 0000:01:00.0; empty accepts any slot

MEM_LIMIT=2G                       # memory cgroup for Filebench and db_bench; keep 2G
