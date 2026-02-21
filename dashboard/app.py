import os
import streamlit as st

st.set_page_config(
    page_title="Crypto Arb Bot",
    page_icon="",
    layout="wide",
    initial_sidebar_state="expanded",
)

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

if trades:
    st.subheader("Latest Trades")
    import pandas as pd

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
