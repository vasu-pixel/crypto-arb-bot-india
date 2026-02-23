#!/usr/bin/env python3
import os
import re

def process_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()
    
    # Patch point 1: endpoint
    content = re.sub(r'~endpoint<connection,config>\(\) \{\}', r'~endpoint() {}', content)
    
    # Patch point 2: connection
    content = re.sub(r'~connection<config>\(\) \{\}', r'~connection() {}', content)
    
    # Patch point 3: server_endpoint
    content = re.sub(r'~server<config>\(\) \{\}', r'~server() {}', content)
    content = re.sub(r'server<config>\(server<config> &\)', r'server(server &)', content)
    content = re.sub(r'server<config>\(server<config> && o\)', r'server(server && o)', content)
    
    # Patch point 4: client_endpoint
    content = re.sub(r'~client<config>\(\) \{\}', r'~client() {}', content)
    content = re.sub(r'client<config>\(client<config> const &\)', r'client(client const &)', content)
    content = re.sub(r'client<config>\(client<config> && o\)', r'client(client && o)', content)
    
    # Patch point 5: logger/basic
    content = re.sub(r'~basic<concurrency,names>\(\) \{\}', r'~basic() {}', content)
    content = re.sub(r'basic<concurrency,names>\(', r'basic(', content)
    
    with open(filepath, 'w') as f:
        f.write(content)

search_dir = 'build/_deps/websocketpp-src/websocketpp'
if os.path.exists(search_dir):
    for root, dirs, files in os.walk(search_dir):
        for file in files:
            if file.endswith('.hpp'):
                process_file(os.path.join(root, file))
    print("Websocketpp C++20 patch completed.")
else:
    print("Directory not found:", search_dir)
