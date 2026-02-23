import csv
import time
import random
import os

def generate_mock_data():
    os.makedirs('data/historical', exist_ok=True)
    filename = 'data/historical/mock_data.csv'
    
    with open(filename, 'w', newline='') as f:
        writer = csv.writer(f)
        
        # header
        header = ['timestamp_ms', 'exchange', 'pair']
        for i in range(20):
            header.extend([f'bid{i}_price', f'bid{i}_qty'])
        for i in range(20):
            header.extend([f'ask{i}_price', f'ask{i}_qty'])
        writer.writerow(header)
        
        base_time = int(time.time() * 1000) - 86400000  # 1 day ago
        pairs = ['BTC/USDT', 'ETH/USDT']
        exchanges = ['BINANCE', 'OKX', 'BYBIT']
        
        base_prices = {'BTC/USDT': 50000.0, 'ETH/USDT': 3000.0}
        
        for t in range(100):
            current_time = base_time + t * 1000
            for pair in pairs:
                for exchange in exchanges:
                    row = [current_time, exchange, pair]
                    
                    price = base_prices[pair]
                    # simulate slight price variations
                    price += random.uniform(-10, 10)
                    
                    # Bids
                    for i in range(20):
                        bid_price = price - (i * 0.5) - random.uniform(0.1, 1.0)
                        bid_qty = random.uniform(0.1, 2.0)
                        row.extend([f"{bid_price:.2f}", f"{bid_qty:.4f}"])
                        
                    # Asks
                    for i in range(20):
                        ask_price = price + (i * 0.5) + random.uniform(0.1, 1.0)
                        ask_qty = random.uniform(0.1, 2.0)
                        row.extend([f"{ask_price:.2f}", f"{ask_qty:.4f}"])
                        
                    writer.writerow(row)
                    
    print(f"Generated mock data at {filename}")

if __name__ == '__main__':
    generate_mock_data()
