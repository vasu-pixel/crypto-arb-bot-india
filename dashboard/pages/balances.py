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


def get_asset_usd_prices(prices, exchange_name):
    """Extract mid prices for each base asset from live price data.
    Returns dict like {"BTC": 64389.0, "ETH": 1851.5, "SOL": 78.23}
    Uses per-exchange price when available, falls back to average across exchanges.
    """
    asset_prices = {}
    for pair, exchange_prices in prices.items():
        # Parse pair like "BTC-USDT" -> base = "BTC"
        parts = pair.split("-")
        if len(parts) != 2:
            continue
        base, quote = parts
        if quote not in ("USDT", "USD", "USDC"):
            continue

        # Try to find price for this specific exchange first
        mid = 0.0
        for ep in exchange_prices:
            bid = ep.get("bid", 0)
            ask = ep.get("ask", 0)
            if bid > 0 and ask > 0:
                ep_mid = (bid + ask) / 2
                if ep.get("exchange", "").upper() == exchange_name.upper():
                    mid = ep_mid
                    break
                elif mid == 0.0:
                    mid = ep_mid  # fallback to first valid price

        if mid > 0:
            asset_prices[base] = mid

    return asset_prices


receiver = get_ws_receiver()

st.title("Exchange Balances")

balances = receiver.get_balances()
prices = receiver.get_prices()

if balances:
    # Compute asset prices per exchange
    asset_prices_cache = {}
    for exchange in balances:
        asset_prices_cache[exchange] = get_asset_usd_prices(prices, exchange)

    # Per-exchange balance cards
    cols = st.columns(len(balances))
    grand_total = 0.0
    for i, (exchange, assets) in enumerate(sorted(balances.items())):
        asset_prices = asset_prices_cache.get(exchange, {})
        with cols[i]:
            st.subheader(exchange)
            total_usd = 0.0
            for asset, amount in sorted(assets.items()):
                if isinstance(amount, (int, float)):
                    if asset in ("USD", "USDT", "USDC"):
                        usd_val = amount
                    elif asset in asset_prices:
                        usd_val = amount * asset_prices[asset]
                    else:
                        usd_val = 0.0
                    total_usd += usd_val
                    # Show balance with USD value
                    if asset in ("USD", "USDT", "USDC"):
                        st.text(f"{asset}: {amount:.2f}")
                    elif asset in asset_prices:
                        st.text(f"{asset}: {amount:.6f}  (${usd_val:.2f})")
                    else:
                        st.text(f"{asset}: {amount:.6f}")
            st.metric("Portfolio Value", f"${total_usd:,.2f}")
            grand_total += total_usd

    # Grand total across all exchanges
    st.divider()
    st.metric("Total Portfolio Value (All Exchanges)", f"${grand_total:,.2f}")

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
        usd_values = []
        for exchange, assets in sorted(balances.items()):
            exchange_names.append(exchange)
            amt = assets.get(selected_asset, 0)
            amounts.append(amt)
            # Compute USD value
            asset_prices = asset_prices_cache.get(exchange, {})
            if selected_asset in ("USD", "USDT", "USDC"):
                usd_values.append(amt)
            elif selected_asset in asset_prices:
                usd_values.append(amt * asset_prices[selected_asset])
            else:
                usd_values.append(0)

        fig = go.Figure(
            data=[
                go.Bar(
                    x=exchange_names,
                    y=amounts,
                    text=[f"{a:.6f}<br>${u:.2f}" for a, u in zip(amounts, usd_values)],
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
            for exch, amt, usd_val in zip(exchange_names, amounts, usd_values):
                drift_pct = ((amt - avg) / avg * 100) if avg > 0 else 0
                drift_data.append(
                    {
                        "Exchange": exch,
                        "Balance": f"{amt:.6f}",
                        "USD Value": f"${usd_val:.2f}",
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
