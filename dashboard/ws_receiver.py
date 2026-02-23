import websocket
import threading
import json
import time
import logging
from collections import deque

logger = logging.getLogger(__name__)


class WsReceiver(threading.Thread):
    """
    Background daemon thread that maintains a persistent WebSocket connection
    to the C++ arb bot server. Stores received data in thread-safe deques
    that Streamlit pages read from.
    """

    def __init__(self, url="ws://localhost:9003", max_buffer=1000):
        super().__init__(daemon=True)
        self.url = url
        self.max_buffer = max_buffer
        self.running = True
        self.connected = False
        self.last_heartbeat = 0
        self.reconnect_delay = 1

        self.trades = deque(maxlen=max_buffer)
        self.spreads = {}
        self.balances = {}
        self.pnl = {}
        self.prices = {}
        self.alerts = deque(maxlen=100)
        self.connection_info = {"dropped_count": 0, "seq": 0}
        self._lock = threading.Lock()

    def run(self):
        while self.running:
            try:
                ws = websocket.WebSocketApp(
                    self.url,
                    on_message=self._on_message,
                    on_error=self._on_error,
                    on_close=self._on_close,
                    on_open=self._on_open,
                    on_ping=self._on_ping,
                )
                ws.run_forever(
                    ping_interval=10,
                    ping_timeout=5,
                    reconnect=0,
                )
            except Exception as e:
                logger.error(f"WS connection failed: {e}")

            if self.running:
                logger.info(f"Reconnecting in {self.reconnect_delay}s...")
                time.sleep(self.reconnect_delay)
                self.reconnect_delay = min(self.reconnect_delay * 2, 30)

    def _on_open(self, ws):
        self.connected = True
        self.reconnect_delay = 1
        logger.info("Connected to arb bot server")

    def _on_message(self, ws, message):
        try:
            msg = json.loads(message)
            msg_type = msg.get("type")
            data = msg.get("data", {})

            with self._lock:
                if msg_type == "trade":
                    self.trades.appendleft(data)
                elif msg_type == "spreads":
                    # Merge per-pair: each message has {pair: {routes}}
                    self.spreads.update(data)
                elif msg_type == "balances":
                    self.balances = data
                elif msg_type == "pnl":
                    self.pnl = data
                elif msg_type == "prices":
                    self.prices = data
                elif msg_type == "heartbeat":
                    self.last_heartbeat = time.time()
                    self.connection_info["seq"] = data.get("seq", 0)
                    self.connection_info["dropped_count"] = data.get(
                        "dropped_count", 0
                    )
                elif msg_type == "alert":
                    self.alerts.appendleft(data)
        except json.JSONDecodeError:
            logger.warning("Received malformed JSON from server")

    def _on_error(self, ws, error):
        logger.error(f"WS error: {error}")
        self.connected = False

    def _on_close(self, ws, close_status_code, close_msg):
        self.connected = False
        logger.info(f"WS closed: {close_status_code} {close_msg}")

    def _on_ping(self, ws, data):
        pass

    def get_trades(self, n=50):
        with self._lock:
            return list(self.trades)[:n]

    def get_spreads(self):
        with self._lock:
            return dict(self.spreads)

    def get_balances(self):
        with self._lock:
            return dict(self.balances)

    def get_pnl(self):
        with self._lock:
            return dict(self.pnl)

    def get_prices(self):
        with self._lock:
            return dict(self.prices)

    def get_alerts(self, n=20):
        with self._lock:
            return list(self.alerts)[:n]

    def is_healthy(self):
        return self.connected and (time.time() - self.last_heartbeat < 15)

    def stop(self):
        self.running = False
