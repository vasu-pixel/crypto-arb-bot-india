import streamlit as st
import pandas as pd
import plotly.graph_objects as go
from streamlit_autorefresh import st_autorefresh

st.set_page_config(page_title="Spreads", layout="wide")
st_autorefresh(interval=1000, limit=None, key="spreads_refresh")

from ws_receiver import WsReceiver
import os


@st.cache_resource
def get_ws_receiver():
    url = os.environ.get("ARB_BOT_WS_URL", "ws://localhost:9002")
    receiver = WsReceiver(url=url)
    receiver.start()
    return receiver


receiver = get_ws_receiver()
EXCHANGES = ["BINANCE_US", "KRAKEN", "COINBASE"]

st.title("Spread Monitor")

# --- Live Prices & Latency ---
prices = receiver.get_prices()
if prices:
    st.subheader("Live Prices by Exchange")
    for pair in sorted(prices.keys()):
        exchange_prices = prices[pair]
        if not exchange_prices:
            continue
        cols = st.columns(len(exchange_prices))
        for i, ep in enumerate(exchange_prices):
            exch_name = ep.get("exchange", "").replace("_", " ")
            bid = ep.get("bid", 0)
            ask = ep.get("ask", 0)
            age_ms = ep.get("age_ms", 0)

            if age_ms < 1000:
                latency_str = f"{age_ms}ms"
                latency_color = "green" if age_ms < 500 else "orange"
            elif age_ms < 10000:
                latency_str = f"{age_ms / 1000:.1f}s"
                latency_color = "orange"
            else:
                latency_str = f"{age_ms / 1000:.0f}s"
                latency_color = "red"

            with cols[i]:
                mid = (bid + ask) / 2 if bid > 0 and ask > 0 else 0
                if mid >= 1:
                    price_str = f"${mid:,.2f}"
                else:
                    price_str = f"${mid:.6f}"
                st.metric(
                    label=f"{pair} @ {exch_name}",
                    value=price_str,
                    delta=f"Latency: {latency_str}",
                )
                if bid > 0 and ask > 0:
                    spread_pct = (ask - bid) / bid * 100
                    bid_str = f"${bid:,.2f}" if bid >= 1 else f"${bid:.6f}"
                    ask_str = f"${ask:,.2f}" if ask >= 1 else f"${ask:.6f}"
                    st.caption(f"Bid: {bid_str} | Ask: {ask_str} | Spread: {spread_pct:.4f}%")
    st.divider()

# --- Spread Heatmap ---
spreads = receiver.get_spreads()

if spreads:
    pairs = sorted(spreads.keys())
    selected_pair = st.selectbox("Select Pair", pairs, index=0)

    if selected_pair and selected_pair in spreads:
        pair_spreads = spreads[selected_pair]

        st.subheader(f"{selected_pair} Net Spreads (bps)")

        # Build spread matrix
        matrix_data = []
        for buy_exch in EXCHANGES:
            row = []
            for sell_exch in EXCHANGES:
                if buy_exch == sell_exch:
                    row.append(None)
                else:
                    key = f"{buy_exch}->{sell_exch}"
                    spread_info = pair_spreads.get(key, {})
                    net_bps = spread_info.get("net_bps", 0) if isinstance(spread_info, dict) else 0
                    row.append(net_bps)
            matrix_data.append(row)

        # Heatmap
        fig = go.Figure(
            data=go.Heatmap(
                z=matrix_data,
                x=[e.replace("_", " ") for e in EXCHANGES],
                y=[e.replace("_", " ") for e in EXCHANGES],
                colorscale=[
                    [0, "rgb(255, 50, 50)"],
                    [0.5, "rgb(255, 255, 255)"],
                    [1, "rgb(50, 200, 50)"],
                ],
                zmid=0,
                text=[
                    [f"{v:.2f}" if v is not None else "---" for v in row]
                    for row in matrix_data
                ],
                texttemplate="%{text}",
                hovertemplate="Buy: %{y}<br>Sell: %{x}<br>Net Spread: %{z:.2f} bps<extra></extra>",
            )
        )
        fig.update_layout(
            xaxis_title="Sell Exchange",
            yaxis_title="Buy Exchange",
            height=400,
        )
        st.plotly_chart(fig, use_container_width=True)

        # Table view
        st.subheader("Spread Details")
        table_rows = []
        for key, info in pair_spreads.items():
            if isinstance(info, dict):
                table_rows.append(
                    {
                        "Route": key.replace("_", " "),
                        "Gross (bps)": f"{info.get('gross_bps', 0):.2f}",
                        "Net (bps)": f"{info.get('net_bps', 0):.2f}",
                        "Profitable": "Yes" if info.get("net_bps", 0) > 0 else "No",
                    }
                )
        if table_rows:
            st.dataframe(
                pd.DataFrame(table_rows), use_container_width=True, hide_index=True
            )

    # All pairs overview
    st.subheader("All Pairs - Best Spreads")
    best_spreads = []
    for pair, pair_data in spreads.items():
        best_net = float("-inf")
        best_route = ""
        for route, info in pair_data.items():
            if isinstance(info, dict):
                net = info.get("net_bps", 0)
                if net > best_net:
                    best_net = net
                    best_route = route
        best_spreads.append(
            {
                "Pair": pair,
                "Best Route": best_route.replace("_", " "),
                "Net Spread (bps)": f"{best_net:.2f}",
                "Actionable": "Yes" if best_net > 0 else "No",
            }
        )
    if best_spreads:
        st.dataframe(
            pd.DataFrame(best_spreads), use_container_width=True, hide_index=True
        )
else:
    st.info("Waiting for spread data from the bot...")
