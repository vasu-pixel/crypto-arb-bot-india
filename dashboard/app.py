import os
import streamlit as st
from streamlit_autorefresh import st_autorefresh

st.set_page_config(
    page_title="Crypto Arb Bot",
    page_icon="",
    layout="wide",
    initial_sidebar_state="expanded",
)

# Auto-refresh every 2 seconds so connection status and metrics stay current
st_autorefresh(interval=2000, key="home_refresh")

from ws_receiver import WsReceiver


@st.cache_resource
def get_ws_receiver():
    url = os.environ.get("ARB_BOT_WS_URL", "ws://localhost:9002")
    receiver = WsReceiver(url=url)
    receiver.start()
    return receiver


receiver = get_ws_receiver()

# Header
col1, col2, col3 = st.columns([5, 1, 1])
with col1:
    st.title("Crypto Arbitrage Dashboard")
with col2:
    mode = os.environ.get("ARB_BOT_MODE", "UNKNOWN")
    if mode == "PAPER":
        st.warning("PAPER")
    elif mode == "LIVE":
        st.error("LIVE")
    elif mode == "BACKTEST":
        st.info("BACKTEST")
    else:
        st.info(mode)
with col3:
    if receiver.is_healthy():
        st.success("CONNECTED")
    else:
        st.error("DISCONNECTED")

# Sidebar
with st.sidebar:
    st.header("Navigation")
    st.page_link("app.py", label="Home", icon="🏠")
    st.page_link("pages/live_trades.py", label="Live Trades", icon="📊")
    st.page_link("pages/spreads.py", label="Spreads", icon="📈")
    st.page_link("pages/balances.py", label="Balances", icon="💰")
    st.page_link("pages/pnl.py", label="P&L", icon="💹")
    st.page_link("pages/backtest.py", label="Backtest", icon="🔬")

    st.divider()
    st.caption("Connection Info")
    info = receiver.connection_info
    st.text(f"Seq: {info.get('seq', 0)}")
    st.text(f"Dropped: {info.get('dropped_count', 0)}")

# Home page content
st.subheader("Quick Overview")

pnl_data = receiver.get_pnl()
balances = receiver.get_balances()
trades = receiver.get_trades(n=5)

col1, col2, col3, col4 = st.columns(4)
with col1:
    st.metric("Total P&L", f"${pnl_data.get('total_pnl', 0):.2f}")
with col2:
    st.metric("Total Trades", pnl_data.get("total_trades", 0))
with col3:
    st.metric("Win Rate", f"{pnl_data.get('win_rate', 0):.1f}%")
with col4:
    exchange_count = len(balances)
    st.metric("Exchanges", exchange_count)

import pandas as pd

# Live Prices
prices = receiver.get_prices()
if prices:
    st.subheader("Live Prices")
    price_rows = []
    for pair, exchange_prices in sorted(prices.items()):
        for ep in exchange_prices:
            bid = ep.get("bid", 0)
            ask = ep.get("ask", 0)
            if bid <= 0 and ask <= 0:
                continue  # Skip exchanges with no data
            age_ms = ep.get("age_ms", 0)
            if age_ms < 1000:
                latency_str = f"{age_ms}ms"
            else:
                latency_str = f"{age_ms / 1000:.1f}s"
            spread_pct = ((ask - bid) / bid * 100) if bid > 0 else 0
            price_rows.append({
                "Pair": pair,
                "Exchange": ep.get("exchange", ""),
                "Bid": f"${bid:,.2f}" if bid >= 1 else f"${bid:.6f}",
                "Ask": f"${ask:,.2f}" if ask >= 1 else f"${ask:.6f}",
                "Spread": f"{spread_pct:.4f}%",
                "Latency": latency_str,
            })
    if price_rows:
        st.dataframe(
            pd.DataFrame(price_rows), use_container_width=True, hide_index=True
        )

if trades:
    st.subheader("Latest Trades")

    df = pd.DataFrame(trades[:5])
    if not df.empty:
        display_cols = [
            c
            for c in [
                "pair",
                "buy_exchange",
                "sell_exchange",
                "quantity",
                "net_spread_bps",
                "realized_pnl",
            ]
            if c in df.columns
        ]
        if display_cols:
            st.dataframe(df[display_cols], use_container_width=True, hide_index=True)

alerts = receiver.get_alerts(n=5)
if alerts:
    st.subheader("Recent Alerts")
    for alert in alerts:
        st.warning(alert.get("message", str(alert)))
