#!/bin/bash
set -euo pipefail

# Configuration
PROJECT_ID="${GCP_PROJECT_ID:-your-gcp-project}"
ZONE="${GCP_ZONE:-us-central1-a}"
INSTANCE="${GCP_INSTANCE:-arb-bot-vm}"

echo "=== Deploying Crypto Arb Bot to GCP ==="
echo "Project: $PROJECT_ID"
echo "Zone: $ZONE"
echo "Instance: $INSTANCE"

# Sync code to VM
echo "=== Syncing code ==="
gcloud compute scp --recurse \
    --zone="$ZONE" \
    --project="$PROJECT_ID" \
    --compress \
    . "$INSTANCE:/opt/arb-bot/"

# Deploy on VM
echo "=== Building and deploying ==="
gcloud compute ssh "$INSTANCE" \
    --zone="$ZONE" \
    --project="$PROJECT_ID" \
    --command="
        cd /opt/arb-bot
        docker compose -f docker/docker-compose.yml build --parallel
        docker compose -f docker/docker-compose.yml down
        docker compose -f docker/docker-compose.yml up -d
        echo '=== Deployment complete ==='
        docker compose -f docker/docker-compose.yml ps
        echo '=== Recent logs ==='
        docker compose -f docker/docker-compose.yml logs --tail=20
    "

echo "=== Done ==="
echo "Dashboard: http://$(gcloud compute instances describe $INSTANCE --zone=$ZONE --project=$PROJECT_ID --format='get(networkInterfaces[0].accessConfigs[0].natIP)'):8501"
