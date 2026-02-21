#!/bin/bash
set -euo pipefail

echo "=== Setting up development environment ==="

# Detect OS
if [[ "$(uname)" == "Darwin" ]]; then
    echo "macOS detected"
    # Install dependencies via Homebrew
    if ! command -v brew &>/dev/null; then
        echo "Homebrew not found. Install from https://brew.sh"
        exit 1
    fi
    brew install cmake boost openssl curl
    echo "System dependencies installed."

elif [[ "$(uname)" == "Linux" ]]; then
    echo "Linux detected"
    sudo apt-get update
    sudo apt-get install -y \
        build-essential cmake git \
        libcurl4-openssl-dev libssl-dev \
        libboost-all-dev \
        python3 python3-pip python3-venv
    echo "System dependencies installed."
fi

# Build C++ backend
echo "=== Building C++ backend ==="
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build . -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
echo "Build complete."

# Run tests
echo "=== Running tests ==="
ctest --output-on-failure || true

cd ..

# Set up Python dashboard
echo "=== Setting up Python dashboard ==="
python3 -m venv dashboard/.venv
source dashboard/.venv/bin/activate
pip install -r dashboard/requirements.txt
echo "Python environment ready."

# Create data directories
mkdir -p data/historical logs

# Copy .env template if .env doesn't exist
if [[ ! -f .env ]]; then
    cp .env.example .env
    echo "Created .env from template. Please fill in your API keys."
fi

echo ""
echo "=== Setup complete ==="
echo ""
echo "To run the bot:    ./build/arb_bot --config config/config.json"
echo "To run dashboard:  cd dashboard && source .venv/bin/activate && streamlit run app.py"
echo "To run backtest:   ./build/arb_bot --backtest --from 2026-01-01 --to 2026-02-01"
echo "To deploy to GCP:  bash scripts/deploy_gcp.sh"
