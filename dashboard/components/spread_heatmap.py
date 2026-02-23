import plotly.graph_objects as go

EXCHANGES = ["BINANCE", "OKX", "BYBIT"]


def create_spread_heatmap(pair_spreads, pair_name=""):
    """Create a heatmap showing net spreads between exchange pairs."""
    matrix_data = []
    for buy_exch in EXCHANGES:
        row = []
        for sell_exch in EXCHANGES:
            if buy_exch == sell_exch:
                row.append(None)
            else:
                key = f"{buy_exch}->{sell_exch}"
                info = pair_spreads.get(key, {})
                net_bps = info.get("net_bps", 0) if isinstance(info, dict) else 0
                row.append(net_bps)
        matrix_data.append(row)

    fig = go.Figure(
        data=go.Heatmap(
            z=matrix_data,
            x=[e.replace("_", ".") for e in EXCHANGES],
            y=[e.replace("_", ".") for e in EXCHANGES],
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
            hovertemplate="Buy: %{y}<br>Sell: %{x}<br>Net: %{z:.2f} bps<extra></extra>",
        )
    )
    fig.update_layout(
        title=f"{pair_name} Net Spread (bps)" if pair_name else "Net Spread (bps)",
        xaxis_title="Sell Exchange",
        yaxis_title="Buy Exchange",
        height=400,
    )
    return fig
