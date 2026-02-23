import streamlit as st
import pandas as pd
import plotly.graph_objects as go
from streamlit_autorefresh import st_autorefresh

st.set_page_config(page_title="Balances", layout="wide")
st_autorefresh(interval=5000, limit=None, key="balances_refresh")

from ws_receiver import WsReceiver
import os


@st.cache_resource
def get_ws_receiver():
    url = os.environ.get("ARB_BOT_WS_URL", "ws://localhost:9003")
    receiver = WsReceiver(url=url)
    receiver.start()
    return receiver


receiver = get_ws_receiver()

st.title("Exchange Balances")

balances = receiver.get_balances()

if balances:
    # Per-exchange balance cards
    cols = st.columns(len(balances))
    for i, (exchange, assets) in enumerate(sorted(balances.items())):
        with cols[i]:
            st.subheader(exchange)
            total_usd = 0
            for asset, amount in sorted(assets.items()):
                if isinstance(amount, (int, float)):
                    st.text(f"{asset}: {amount:.6f}")
                    if asset in ("USD", "USDT", "USDC"):
                        total_usd += amount
            st.metric("USDT Value", f"${total_usd:,.2f}")

    st.divider()

    # Cross-exchange comparison chart
    st.subheader("Asset Distribution Across Exchanges")
    all_assets = set()
    for assets in balances.values():
        all_assets.update(assets.keys())

    selected_asset = st.selectbox(
        "Select Asset",
        sorted(all_assets),
        index=0 if all_assets else None,
    )

    if selected_asset:
        exchange_names = []
        amounts = []
        for exchange, assets in sorted(balances.items()):
            exchange_names.append(exchange)
            amounts.append(assets.get(selected_asset, 0))

        fig = go.Figure(
            data=[
                go.Bar(
                    x=exchange_names,
                    y=amounts,
                    text=[f"{a:.6f}" for a in amounts],
                    textposition="auto",
                )
            ]
        )
        fig.update_layout(
            title=f"{selected_asset} Balance by Exchange",
            yaxis_title=selected_asset,
            height=400,
        )
        st.plotly_chart(fig, use_container_width=True)

        # Drift analysis
        if len(amounts) > 1 and max(amounts) > 0:
            avg = sum(amounts) / len(amounts)
            st.subheader("Inventory Drift Analysis")
            drift_data = []
            for exch, amt in zip(exchange_names, amounts):
                drift_pct = ((amt - avg) / avg * 100) if avg > 0 else 0
                drift_data.append(
                    {
                        "Exchange": exch,
                        "Balance": f"{amt:.6f}",
                        "Avg": f"{avg:.6f}",
                        "Drift %": f"{drift_pct:+.2f}%",
                    }
                )
            st.dataframe(
                pd.DataFrame(drift_data), use_container_width=True, hide_index=True
            )

    # Drift alerts
    alerts = receiver.get_alerts(n=10)
    if alerts:
        st.subheader("Drift Alerts")
        for alert in alerts:
            msg = alert.get("message", str(alert))
            imbalance = alert.get("imbalance_pct", 0)
            if imbalance > 30:
                st.error(msg)
            elif imbalance > 20:
                st.warning(msg)
            else:
                st.info(msg)
else:
    st.info("Waiting for balance data from the bot...")
