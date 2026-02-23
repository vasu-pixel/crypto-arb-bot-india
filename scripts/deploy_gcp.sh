#!/bin/bash
set -euo pipefail

# ────────────────────────────────────────────────────────────────
# Crypto Arb Bot – GCP Deployment Script
# ────────────────────────────────────────────────────────────────
#
# Prerequisites:
#   1. gcloud CLI installed and authenticated (gcloud auth login)
#   2. Docker installed on the VM (the script installs it if missing)
#   3. .env file at repo root with API keys (see .env.example)
#
# Usage:
#   GCP_PROJECT_ID=my-project ./scripts/deploy_gcp.sh
#
# Environment variables (override defaults):
#   GCP_PROJECT_ID  – your GCP project ID  (required)
#   GCP_ZONE        – VM zone              (default: asia-south1-a, Mumbai)
#   GCP_INSTANCE    – VM name              (default: arb-bot-vm)
#   MACHINE_TYPE    – VM machine type       (default: e2-medium)
# ────────────────────────────────────────────────────────────────

PROJECT_ID="${GCP_PROJECT_ID:?Set GCP_PROJECT_ID}"
ZONE="${GCP_ZONE:-asia-south1-a}"
INSTANCE="${GCP_INSTANCE:-arb-bot-vm}"
MACHINE_TYPE="${MACHINE_TYPE:-e2-medium}"

echo "=== Deploying Crypto Arb Bot to GCP ==="
echo "Project:  $PROJECT_ID"
echo "Zone:     $ZONE"
echo "Instance: $INSTANCE"
echo "Machine:  $MACHINE_TYPE"
echo ""

# ── Step 1: Create VM if it doesn't exist ──────────────────────
if ! gcloud compute instances describe "$INSTANCE" \
      --zone="$ZONE" --project="$PROJECT_ID" &>/dev/null; then
    echo "=== Creating VM ==="
    gcloud compute instances create "$INSTANCE" \
        --zone="$ZONE" \
        --project="$PROJECT_ID" \
        --machine-type="$MACHINE_TYPE" \
        --image-family=ubuntu-2204-lts \
        --image-project=ubuntu-os-cloud \
        --boot-disk-size=30GB \
        --tags=arb-bot \
        --metadata=startup-script='#!/bin/bash
            apt-get update
            apt-get install -y docker.io docker-compose-plugin
            systemctl enable docker
            usermod -aG docker $USER
        '

    echo "Waiting 60s for VM startup script to finish..."
    sleep 60
fi

# ── Step 2: Open firewall for dashboard (8501) ─────────────────
if ! gcloud compute firewall-rules describe allow-arb-dashboard \
      --project="$PROJECT_ID" &>/dev/null; then
    echo "=== Creating firewall rule for dashboard port 8501 ==="
    gcloud compute firewall-rules create allow-arb-dashboard \
        --project="$PROJECT_ID" \
        --allow=tcp:8501 \
        --target-tags=arb-bot \
        --description="Allow Streamlit dashboard access"
fi

# ── Step 3: Sync code to VM ───────────────────────────────────
echo "=== Syncing code ==="
gcloud compute scp --recurse \
    --zone="$ZONE" \
    --project="$PROJECT_ID" \
    --compress \
    --exclude='build,.git,build/_deps' \
    . "$INSTANCE:/opt/arb-bot/"

# ── Step 4: Build & deploy on VM ──────────────────────────────
echo "=== Building and deploying ==="
gcloud compute ssh "$INSTANCE" \
    --zone="$ZONE" \
    --project="$PROJECT_ID" \
    --command="
        cd /opt/arb-bot

        # Ensure docker is available
        if ! command -v docker &>/dev/null; then
            sudo apt-get update && sudo apt-get install -y docker.io docker-compose-plugin
            sudo systemctl enable docker && sudo systemctl start docker
            sudo usermod -aG docker \$USER
            echo 'Docker installed. You may need to re-run this script.'
            exit 1
        fi

        # Build and start
        sudo docker compose -f docker/docker-compose.yml build --parallel
        sudo docker compose -f docker/docker-compose.yml down 2>/dev/null || true
        sudo docker compose -f docker/docker-compose.yml up -d

        echo ''
        echo '=== Container Status ==='
        sudo docker compose -f docker/docker-compose.yml ps
        echo ''
        echo '=== Recent Logs ==='
        sudo docker compose -f docker/docker-compose.yml logs --tail=30
    "

# ── Done ──────────────────────────────────────────────────────
EXTERNAL_IP=$(gcloud compute instances describe "$INSTANCE" \
    --zone="$ZONE" --project="$PROJECT_ID" \
    --format='get(networkInterfaces[0].accessConfigs[0].natIP)')

echo ""
echo "=== Deployment Complete ==="
echo "Dashboard: http://${EXTERNAL_IP}:8501"
echo "Bot WS:    ws://${EXTERNAL_IP}:9003"
echo ""
echo "Useful commands:"
echo "  # SSH into VM"
echo "  gcloud compute ssh $INSTANCE --zone=$ZONE --project=$PROJECT_ID"
echo ""
echo "  # View live logs"
echo "  gcloud compute ssh $INSTANCE --zone=$ZONE --project=$PROJECT_ID \\"
echo "    --command='cd /opt/arb-bot && sudo docker compose -f docker/docker-compose.yml logs -f'"
echo ""
echo "  # Stop the bot"
echo "  gcloud compute ssh $INSTANCE --zone=$ZONE --project=$PROJECT_ID \\"
echo "    --command='cd /opt/arb-bot && sudo docker compose -f docker/docker-compose.yml down'"
