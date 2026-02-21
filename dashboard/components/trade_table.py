import pandas as pd
import streamlit as st


def render_trade_table(trades, max_rows=50):
    """Render a styled trade table."""
    if not trades:
        st.info("No trades available.")
        return

    df = pd.DataFrame(trades[:max_rows])

    display_cols = [
        c
        for c in [
            "pair",
            "buy_exchange",
            "sell_exchange",
            "quantity",
            "buy_fill_price",
            "sell_fill_price",
            "net_spread_bps",
            "realized_pnl",
            "mode",
            "timestamp",
        ]
        if c in df.columns
    ]

    if not display_cols:
        st.dataframe(df, use_container_width=True, hide_index=True)
        return

    styled = df[display_cols].copy()
    if "realized_pnl" in styled.columns:
        styled["realized_pnl"] = styled["realized_pnl"].apply(
            lambda x: f"${x:.4f}" if pd.notna(x) else ""
        )

    st.dataframe(styled, use_container_width=True, hide_index=True)
