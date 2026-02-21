import plotly.graph_objects as go


def create_balance_chart(balances, asset="USD"):
    """Create a bar chart showing an asset's balance across exchanges."""
    exchange_names = []
    amounts = []

    for exchange, assets in sorted(balances.items()):
        exchange_names.append(exchange.replace("_", "."))
        amounts.append(assets.get(asset, 0))

    fig = go.Figure(
        data=[
            go.Bar(
                x=exchange_names,
                y=amounts,
                text=[f"{a:.6f}" for a in amounts],
                textposition="auto",
                marker_color="rgb(55, 83, 109)",
            )
        ]
    )
    fig.update_layout(
        title=f"{asset} Balance by Exchange",
        yaxis_title=asset,
        height=400,
    )
    return fig
