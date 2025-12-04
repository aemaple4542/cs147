from flask import Flask, request
from datetime import datetime
import json
import os
import matplotlib.pyplot as plt
import numpy as np

app = Flask(__name__)

DATA_FILE = 'wall_sit_data.jsonl'
PLOT_FILE = 'wall_sit_progress.png' 

@app.route('/', methods=['GET'])
def collect_data():
    """Endpoint for the ESP32 to send individual session data."""
    try:
        session_num_str = request.args.get("session_number")
        score_str = request.args.get("session_percentage")

        if not session_num_str or not score_str:
            return "Error: Missing parameters", 400

        raw_score_int = int(float(score_str))
        raw_session_int = int(float(session_num_str))

        final_percentage = raw_score_int
        final_session_num = raw_session_int
        
        session_data = {
            "timestamp": datetime.now().isoformat(),
            "session_number": final_session_num,
            "session_percentage": final_percentage
        }
        
        with open(DATA_FILE, 'a') as f:
            f.write(json.dumps(session_data) + '\n')

        print(f"SESSION LOGGED: Session {final_session_num}, Score: {final_percentage}%")
        
        return f"Session {final_session_num} data received and logged.", 200

    except Exception as e:
        print(f"Error processing request: {e}")
        return "Internal Server Error", 500

def generate_progress_plot(sessions):
    session_numbers = [s['session_number'] for s in sessions]
    percentages = [s['session_percentage'] for s in sessions]
    plt.figure(figsize=(10, 6))
    plt.plot(session_numbers, percentages, marker='o', linestyle='-', color='skyblue', label='Session Score')
    if percentages:
        average = np.mean(percentages)
        plt.axhline(y=average, color='r', linestyle='--', label=f'Avg Score ({average:.2f}%)')
    plt.title('Wall Sit Performance Over Sessions')
    plt.xlabel('Session Number')
    plt.ylabel('Compliance Percentage (%)')
    plt.ylim(0, 105) 
    plt.xticks(np.unique(session_numbers))
    plt.legend()
    plt.grid(True)
    plt.savefig(PLOT_FILE)
    plt.close()

@app.route('/visualize', methods=['GET'])
def visualize_data():
    if not os.path.exists(DATA_FILE):
        return "No session data collected yet.", 200

    sessions = []
    total_percentage_sum = 0
    total_sessions = 0

    with open(DATA_FILE, 'r') as f:
        for line in f:
            try:
                data = json.loads(line)
                sessions.append(data)
                total_percentage_sum += data['session_percentage']
                total_sessions += 1
            except json.JSONDecodeError:
                continue

    if total_sessions == 0:
        return "Data file is empty.", 200
        
    generate_progress_plot(sessions)
        
    average_score = total_percentage_sum / total_sessions
    
    import base64
    with open(PLOT_FILE, "rb") as image_file:
        encoded_string = base64.b64encode(image_file.read()).decode()
    
    summary = f"""
    <!DOCTYPE html>
    <html>
    <head><title>Workout Summary</title></head>
    <body>
        <h1>Wall Sit Workout Summary</h1>
        <p>Data Collected: {total_sessions} sessions.</p>
        <p><b>Overall Average Score: {average_score:.2f}%</b></p>
        
        <h2>Progress Visualization:</h2>
        <img src="data:image/png;base64,{encoded_string}" alt="Wall Sit Performance Graph"/>
        
        <h2>Individual Session Log:</h2>
    """
    
    for s in sessions:
        summary += f"<p>Session {s['session_number']} ({s['timestamp'][:19]}): {s['session_percentage']:.2f}%</p>"
        
    summary += "</body></html>"
        
    return summary, 200

if __name__ == '__main__':
    if not os.path.exists(DATA_FILE):
        with open(DATA_FILE, 'w') as f:
            pass 

    app.run(host='0.0.0.0', port=5000)