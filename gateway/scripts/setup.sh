
#!/bin/bash

if [ "$EUID" -ne 0 ]; then
  echo "run with sudo: sudo ./setup.sh"
  exit 1
fi

cd "$(dirname "$0")/.."
WORK_DIR=$(pwd)

echo "create .cert, .key"
mkdir -p certs
openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
    -keyout certs/server.key \
    -out certs/server.crt \
    -subj "/C=VN/ST=Hanoi/L=Hanoi/O=BKHN/OU=IOT Gateway/CN=192.168.1.10" 2>/dev/null

make clean
make

echo "setup systemd"

SERVICE_FILE="/etc/systemd/system/gateway.service"

LOG_DIR="$WORK_DIR/log"
mkdir -p "$LOG_DIR"

cat <<EOF > $SERVICE_FILE
[Unit]
Description=gateway simulate tcp socket with tls one direction
After=network.target

[Service]
Type=simple
WorkingDirectory=$WORK_DIR
StandardOutput=append:$LOG_DIR/gateway.log
StandardError=append:$LOG_DIR/gateway.log

ExecStart=/sbin/ip netns exec node1 $WORK_DIR/bin/gateway $WORK_DIR/certs/server.crt $WORK_DIR/certs/server.key

Restart=on-failure
RestartSec=2

KillSignal=SIGTERM
TimeoutStopSec=5

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
echo "systemd install service in $SERVICE_FILE"

echo "setup success"