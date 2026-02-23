#!/bin/bash
cd ~/crypto-arb-bot || exit 1
pkill -f 'streamlit' || true
nohup python3 -m streamlit run dashboard/app.py --server.port 8502 < /dev/null > dashboard.log 2>&1 &
echo "Dashboard restarted on port 8502"
