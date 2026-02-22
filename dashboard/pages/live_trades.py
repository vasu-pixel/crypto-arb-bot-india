import streamlit as st
import pandas as pd
from streamlit_autorefresh import st_autorefresh

st.set_page_config(page_title="Live Trades", layout="wide")
st_autorefresh(interval=1000, limit=None, key="trades_refresh")

from ws_receiver import WsReceiver
import os


@st.cache_resource
def get_ws_receiver():
    url = os.environ.get("ARB_BOT_WS_URL", "ws://localhost:9002")
    receiver = WsReceiver(url=url)
    receiver.start()
    return receiver


receiver = get_ws_receiver()

st.title("Live Trades")

# Connection status
col1, col2 = st.columns([6, 1])
with col2:
    if receiver.is_healthy():
        st.success("LIVE")
    else:
        st.error("OFFLINE")

# Trade table
trades = receiver.get_trades(n=50)

if trades:
    df = pd.DataFrame(trades)

    # 24h stats
    st.subheader("24h Statistics")
    col1, col2, col3, col4 = st.columns(4)
    with col1:
        st.metric("Total Trades", len(df))
    with col2:
        if "realized_pnl" in df.columns:
            wins = len(df[df["realized_pnl"] > 0])
            win_rate = (wins / len(df) * 100) if len(df) > 0 else 0
            st.metric("Win Rate", f"{win_rate:.1f}%")
    with col3:
        if "realized_pnl" in df.columns:
            avg_pnl = df["realized_pnl"].mean()
            st.metric("Avg P&L/Trade", f"${avg_pnl:.4f}")
    with col4:
        if "realized_pnl" in df.columns:
            total_pnl = df["realized_pnl"].sum()
            st.metric("Total P&L", f"${total_pnl:.2f}")

    st.subheader("Recent Trades")

    display_cols = [
        c
        for c in [
            "id",
            "pair",
            "buy_exchange",
            "sell_exchange",
            "quantity",
            "buy_fill_price",
            "sell_fill_price",
            "buy_fee",
            "sell_fee",
            "gross_spread_bps",
            "net_spread_bps",
            "realized_pnl",
            "buy_status",
            "sell_status",
            "timestamp",
        ]
        if c in df.columns
    ]

    if display_cols:
        styled_df = df[display_cols].copy()
        if "realized_pnl" in styled_df.columns:
            styled_df["realized_pnl"] = styled_df["realized_pnl"].apply(
                lambda x: f"${x:.4f}" if pd.notna(x) else ""
            )
        st.dataframe(styled_df, use_container_width=True, hide_index=True)
    else:
        st.dataframe(df, use_container_width=True, hide_index=True)

    # Per-pair breakdown
    if "pair" in df.columns and "realized_pnl" in df.columns:
        st.subheader("Per-Pair Summary")
        pair_summary = (
            df.groupby("pair")
            .agg(trades=("pair", "count"), total_pnl=("realized_pnl", "sum"), avg_pnl=("realized_pnl", "mean"))
            .reset_index()
        )
        pair_summary["total_pnl"] = pair_summary["total_pnl"].apply(
            lambda x: f"${x:.4f}"
        )
        pair_summary["avg_pnl"] = pair_summary["avg_pnl"].apply(
            lambda x: f"${x:.4f}"
        )
        st.dataframe(pair_summary, use_container_width=True, hide_index=True)
else:
    st.info("No trades yet. Waiting for arbitrage opportunities...")
