#!/bin/bash
set -e
cd ~/crypto-arb-bot || exit 1

echo 'Pulling code...'
git stash || true
git pull origin master

echo 'Building arb bot...'
rm -rf build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cd ..

echo 'Patching websocketpp C++17 template errors via Python AST script...'
python3 ~/patch_websocketpp.py

cd build
make -j$(nproc)
cd ..

echo 'Stopping existing processes...'
pkill -f 'arb_bot' || true
pkill -f 'streamlit' || true
sleep 2

echo 'Starting frontend dashboard...'
nohup python3 -m streamlit run dashboard/app.py --server.port 8502 > dashboard.log 2>&1 &

echo 'Starting backend...'
export BINANCE_API_KEY=fake_key
export BINANCE_SECRET_KEY=fake_secret
nohup ./build/src/arb_bot --paper --config config/config.json > bot.log 2>&1 &

echo 'Services successfully started!'
