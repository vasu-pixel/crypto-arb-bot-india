import streamlit as st
import pandas as pd
import plotly.graph_objects as go
from streamlit_autorefresh import st_autorefresh

st.set_page_config(page_title="P&L", layout="wide")
st_autorefresh(interval=2000, limit=None, key="pnl_refresh")

from ws_receiver import WsReceiver
import os


@st.cache_resource
def get_ws_receiver():
    url = os.environ.get("ARB_BOT_WS_URL", "ws://localhost:9003")
    receiver = WsReceiver(url=url)
    receiver.start()
    return receiver


receiver = get_ws_receiver()

st.title("Profit & Loss")

pnl_data = receiver.get_pnl()
trades = receiver.get_trades(n=500)

# Total Return banner (portfolio value change including unrealized)
total_return = pnl_data.get("total_return", 0)
initial_pv = pnl_data.get("initial_portfolio_value", 0)
current_pv = pnl_data.get("current_portfolio_value", 0)
return_pct = (total_return / initial_pv * 100) if initial_pv > 0 else 0

if initial_pv > 0:
    tr_color = "green" if total_return >= 0 else "red"
    st.markdown(
        f"""
        <div style="background: linear-gradient(135deg, {'#0a2e1a' if total_return >= 0 else '#2e0a0a'}, #1a1a2e);
                    border: 1px solid {'#1aff1a40' if total_return >= 0 else '#ff1a1a40'};
                    border-radius: 12px; padding: 20px; margin-bottom: 20px;">
            <div style="display: flex; justify-content: space-between; align-items: center;">
                <div>
                    <div style="color: #888; font-size: 14px;">Total Return (Realized + Unrealized)</div>
                    <div style="color: {tr_color}; font-size: 36px; font-weight: bold;">
                        ${total_return:+,.2f} ({return_pct:+.1f}%)
                    </div>
                </div>
                <div style="text-align: right;">
                    <div style="color: #888; font-size: 12px;">Initial Value</div>
                    <div style="color: #ccc; font-size: 18px;">${initial_pv:,.2f}</div>
                    <div style="color: #888; font-size: 12px; margin-top: 4px;">Current Value</div>
                    <div style="color: #ccc; font-size: 18px;">${current_pv:,.2f}</div>
                </div>
            </div>
        </div>
        """,
        unsafe_allow_html=True,
    )

# Top-level metrics
col1, col2, col3, col4 = st.columns(4)
with col1:
    total_pnl = pnl_data.get("total_pnl", 0)
    st.metric("Realized P&L (Trades Only)", f"${total_pnl:.2f}")
with col2:
    total_trades = pnl_data.get("total_trades", 0)
    st.metric("Total Trades", total_trades)
with col3:
    win_rate = pnl_data.get("win_rate", 0)
    st.metric("Win Rate", f"{win_rate:.1f}%")
with col4:
    total_fees = pnl_data.get("total_fees", 0)
    st.metric("Total Fees Paid", f"${total_fees:.2f}")

# Divergence warning: when realized PnL and total return differ significantly
if initial_pv > 0 and abs(total_return - total_pnl) > 1.0:
    divergence = total_return - total_pnl
    st.warning(
        f"Realized P&L (${total_pnl:+.2f}) differs from Total Return "
        f"(${total_return:+.2f}) by ${divergence:+.2f}. "
        f"This gap is due to {'asset depreciation' if divergence < 0 else 'asset appreciation'}, "
        f"transfer fees, and rebalancing costs not captured in trade P&L."
    )

# Equity curve
if trades:
    df = pd.DataFrame(trades)
    if "realized_pnl" in df.columns and "timestamp" in df.columns:
        df_sorted = df.sort_values("timestamp")
        df_sorted["cumulative_pnl"] = df_sorted["realized_pnl"].cumsum()

        fig = go.Figure()
        fig.add_trace(
            go.Scatter(
                x=df_sorted["timestamp"],
                y=df_sorted["cumulative_pnl"],
                fill="tozeroy",
                name="Cumulative P&L",
                line=dict(color="rgb(50, 200, 50)"),
                fillcolor="rgba(50, 200, 50, 0.2)",
            )
        )
        fig.update_layout(
            title="Equity Curve",
            xaxis_title="Time",
            yaxis_title="Cumulative P&L ($)",
            height=400,
        )
        st.plotly_chart(fig, use_container_width=True)

# Per-pair breakdown
pnl_per_pair = pnl_data.get("pnl_per_pair", {})
if pnl_per_pair:
    st.subheader("Per-Pair Breakdown")

    pair_rows = []
    for pair, data in pnl_per_pair.items():
        if isinstance(data, dict):
            pair_rows.append(
                {
                    "Pair": pair,
                    "Trades": data.get("trades", 0),
                    "Total P&L": f"${data.get('total_pnl', 0):.4f}",
                    "Avg P&L/Trade": f"${data.get('avg_pnl', 0):.4f}",
                    "Win Rate": f"{data.get('win_rate', 0):.1f}%",
                }
            )
        elif isinstance(data, (int, float)):
            pair_rows.append(
                {"Pair": pair, "Total P&L": f"${data:.4f}"}
            )

    if pair_rows:
        st.dataframe(
            pd.DataFrame(pair_rows), use_container_width=True, hide_index=True
        )

    # Bar chart of P&L per pair
    pair_names = list(pnl_per_pair.keys())
    pair_pnls = []
    for p in pair_names:
        v = pnl_per_pair[p]
        if isinstance(v, dict):
            pair_pnls.append(v.get("total_pnl", 0))
        else:
            pair_pnls.append(float(v))

    colors = ["rgb(50,200,50)" if p >= 0 else "rgb(255,50,50)" for p in pair_pnls]
    fig = go.Figure(
        data=[
            go.Bar(
                x=pair_names,
                y=pair_pnls,
                marker_color=colors,
                text=[f"${p:.2f}" for p in pair_pnls],
                textposition="auto",
            )
        ]
    )
    fig.update_layout(
        title="P&L by Pair",
        yaxis_title="P&L ($)",
        height=400,
    )
    st.plotly_chart(fig, use_container_width=True)

# Fee breakdown per exchange
st.subheader("Fees Paid Per Exchange")
fees_per_exchange = pnl_data.get("fees_per_exchange", {})
if fees_per_exchange:
    fee_cols = st.columns(len(fees_per_exchange))
    for i, (exch, fee) in enumerate(sorted(fees_per_exchange.items())):
        with fee_cols[i]:
            st.metric(exch, f"${fee:.2f}")
else:
    st.info("No fee data available yet.")
