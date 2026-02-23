import streamlit as st
import pandas as pd
import plotly.graph_objects as go
import json
import os
import subprocess

st.set_page_config(page_title="Backtest", layout="wide")

st.title("Backtest Engine")

# Check for existing backtest results
results_path = os.environ.get("BACKTEST_RESULTS", "data/backtest_results.json")

tab1, tab2 = st.tabs(["Run Backtest", "View Results"])

with tab1:
    st.subheader("Configure Backtest")

    col1, col2 = st.columns(2)
    with col1:
        from_date = st.date_input("From Date")
        min_spread = st.number_input("Min Net Spread (bps)", value=3.0, step=0.5)
        min_trade = st.number_input("Min Trade Size (USDT)", value=50.0, step=10.0)
    with col2:
        to_date = st.date_input("To Date")
        max_trade = st.number_input("Max Trade Size (USDT)", value=5000.0, step=100.0)

    data_dir = st.text_input("Historical Data Directory", value="data/historical")

    if st.button("Run Backtest", type="primary"):
        with st.spinner("Running backtest..."):
            bot_binary = os.environ.get("ARB_BOT_BINARY", "./build/src/arb_bot")
            cmd = [
                bot_binary,
                "--backtest",
                "--from", str(from_date),
                "--to", str(to_date),
                "--config", "config/config.json",
            ]
            try:
                result = subprocess.run(
                    cmd,
                    capture_output=True,
                    text=True,
                    timeout=300,
                )
                st.code(result.stdout, language="text")
                if result.returncode != 0:
                    st.error(f"Backtest failed: {result.stderr}")
                else:
                    st.success("Backtest completed!")
            except FileNotFoundError:
                st.error(
                    "arb_bot binary not found. Build the C++ backend first."
                )
            except subprocess.TimeoutExpired:
                st.error("Backtest timed out after 5 minutes.")

with tab2:
    st.subheader("Backtest Results")

    if os.path.exists(results_path):
        with open(results_path, "r") as f:
            metrics = json.load(f)

        # Key metrics
        col1, col2, col3, col4 = st.columns(4)
        with col1:
            st.metric("Total P&L", f"${metrics.get('total_pnl', 0):.2f}")
        with col2:
            st.metric("Total Trades", metrics.get("total_trades", 0))
        with col3:
            st.metric("Win Rate", f"{metrics.get('win_rate', 0):.1f}%")
        with col4:
            st.metric("Sharpe Ratio", f"{metrics.get('sharpe_ratio', 0):.2f}")

        col5, col6, col7, col8 = st.columns(4)
        with col5:
            st.metric("Max Drawdown", f"${metrics.get('max_drawdown', 0):.2f}")
        with col6:
            st.metric(
                "Max Drawdown %", f"{metrics.get('max_drawdown_pct', 0):.1f}%"
            )
        with col7:
            st.metric("Profit Factor", f"{metrics.get('profit_factor', 0):.2f}")
        with col8:
            st.metric(
                "Total Fees", f"${metrics.get('total_fees_paid', 0):.2f}"
            )

        # Equity curve
        equity_curve = metrics.get("equity_curve", [])
        if equity_curve:
            timestamps = [p[0] if isinstance(p, list) else p.get("timestamp", "") for p in equity_curve]
            values = [p[1] if isinstance(p, list) else p.get("value", 0) for p in equity_curve]

            fig = go.Figure()
            fig.add_trace(
                go.Scatter(
                    x=timestamps,
                    y=values,
                    fill="tozeroy",
                    name="Equity",
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

        # P&L per pair
        pnl_per_pair = metrics.get("pnl_per_pair", {})
        if pnl_per_pair:
            st.subheader("P&L by Pair")
            pair_names = list(pnl_per_pair.keys())
            pair_values = list(pnl_per_pair.values())
            colors = [
                "rgb(50,200,50)" if v >= 0 else "rgb(255,50,50)"
                for v in pair_values
            ]

            fig = go.Figure(
                data=[
                    go.Bar(
                        x=pair_names,
                        y=pair_values,
                        marker_color=colors,
                        text=[f"${v:.4f}" for v in pair_values],
                        textposition="auto",
                    )
                ]
            )
            fig.update_layout(
                yaxis_title="P&L ($)",
                height=400,
            )
            st.plotly_chart(fig, use_container_width=True)

        # Raw metrics JSON
        with st.expander("Raw Metrics JSON"):
            st.json(metrics)
    else:
        st.info(
            f"No backtest results found at {results_path}. "
            "Run a backtest first or check the file path."
        )
