# hwbench Relay Server — Deployment Guide

## 1. Google Cloud Setup (one-time)

### 1.1 Create a Google Cloud Project
1. Go to [console.cloud.google.com](https://console.cloud.google.com).
2. Click **Select a project → New Project**, name it anything (e.g. `hwbench`), click **Create**.

### 1.2 Enable the Google Sheets API
1. Go to **APIs & Services → Library**.
2. Search `Google Sheets API`, click it, click **Enable**.

### 1.3 Create a Service Account
1. Go to **APIs & Services → Credentials**.
2. Click **+ Create Credentials → Service Account**.
3. Name it `hwbench-uploader`, click **Create and Continue**, skip optional steps, click **Done**.
4. Click the service account email you just created.
5. Go to the **Keys** tab → **Add Key → Create New Key → JSON**. A `.json` file downloads — keep it safe.

### 1.4 Create and share a Google Sheet
1. Open [sheets.google.com](https://sheets.google.com) and create a new sheet.
2. Rename the default tab at the bottom (e.g. `hwbench`).
3. Click **Share**, paste the service account email (`...@....iam.gserviceaccount.com`), set role to **Editor**, click **Send**.
4. Get the Sheet ID from the URL:
   ```
   https://docs.google.com/spreadsheets/d/1AbCdEfGhIjKlMnOpQrStUv/edit
                                          ^^^^^^^^^^^^^^^^^^^^^^^^
                                          This is your GSHEET_ID
   ```

### 1.5 Compact the service account JSON
Railway and most platforms require the JSON as a single-line string. Run this on your machine:
```bash
python3 -c "import json; print(json.dumps(json.load(open('path/to/key.json'))))"
```
Copy the output — you will paste it as the `GOOGLE_SERVICE_ACCOUNT_JSON_CONTENT` variable.

---

## 2. Deploy on Railway (recommended)

### 2.1 Connect repo
1. Go to [railway.app](https://railway.app) → **New Project → Deploy from GitHub repo**.
2. Select `hwbench`, set **Root Directory** to `server/`.
3. Railway detects Python automatically and deploys.

### 2.2 Set environment variables
In Railway → your service → **Variables**, add:

| Variable | Value |
|----------|-------|
| `GOOGLE_SERVICE_ACCOUNT_JSON_CONTENT` | Compacted JSON from step 1.5 |
| `GSHEET_ID` | Sheet ID from step 1.4 |
| `GSHEET_TAB` | Tab name (e.g. `hwbench`) |

### 2.3 Get the public URL
Railway assigns a URL like `https://hwbench-relay-XXXX.up.railway.app`.
Update `RELAY_URL` at the top of `run.sh`:
```bash
RELAY_URL="https://hwbench-relay-XXXX.up.railway.app/submit"
```

### 2.4 Verify
```bash
curl https://hwbench-relay-XXXX.up.railway.app/health
# {"status":"ok","gapi_available":true,"sheet_configured":true,"tab_configured":true,"creds_configured":true}
```

---

## 3. Deploy on Ubuntu / Linux VPS

This covers any Ubuntu 20.04+ instance — DigitalOcean, Hetzner, AWS EC2, or similar.

### 3.1 Install dependencies
```bash
sudo apt-get update
sudo apt-get install -y python3 python3-pip python3-venv git
```

### 3.2 Clone and set up the server
```bash
git clone https://github.com/farzandfz/hwbench
cd hwbench/server
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

### 3.3 Set environment variables
Create a file to hold the config (do not commit this file):
```bash
cat > /etc/hwbench-relay.env <<EOF
GOOGLE_SERVICE_ACCOUNT_JSON_CONTENT='<compacted JSON from step 1.5>'
GSHEET_ID=<your sheet id>
GSHEET_TAB=hwbench
EOF
chmod 600 /etc/hwbench-relay.env
```

### 3.4 Run with systemd (persistent, survives reboots)
```bash
sudo tee /etc/systemd/system/hwbench-relay.service <<EOF
[Unit]
Description=hwbench relay server
After=network.target

[Service]
User=$USER
WorkingDirectory=$(pwd)
EnvironmentFile=/etc/hwbench-relay.env
ExecStart=$(pwd)/.venv/bin/uvicorn main:app --host 0.0.0.0 --port 8000
Restart=always

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now hwbench-relay
sudo systemctl status hwbench-relay
```

### 3.5 Optional — expose on port 80 with nginx
```bash
sudo apt-get install -y nginx
sudo tee /etc/nginx/sites-available/hwbench <<EOF
server {
    listen 80;
    server_name _;
    location / {
        proxy_pass http://127.0.0.1:8000;
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
    }
}
EOF
sudo ln -s /etc/nginx/sites-available/hwbench /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl reload nginx
```

Then update `RELAY_URL` in `run.sh` to `http://<your-server-ip>/submit`.

### 3.6 Quick run (no systemd, for testing)
```bash
source .venv/bin/activate
export GOOGLE_SERVICE_ACCOUNT_JSON_CONTENT='<compacted JSON>'
export GSHEET_ID=<your sheet id>
export GSHEET_TAB=hwbench
uvicorn main:app --host 0.0.0.0 --port 8000
```

---

## 4. Deploy on Render (free tier alternative)

1. Create account at [render.com](https://render.com).
2. **New → Web Service** → connect your GitHub repo.
3. Set **Root Directory** to `server`, **Build Command** to `pip install -r requirements.txt`, **Start Command** to `uvicorn main:app --host 0.0.0.0 --port $PORT`.
4. Under **Environment**, add the three variables from section 2.2.
5. Deploy and copy the public URL into `RELAY_URL` in `run.sh`.

---

## 5. Local Testing

```bash
cd server/
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt

export GOOGLE_SERVICE_ACCOUNT_JSON_CONTENT='<compacted JSON>'
export GSHEET_ID=<your sheet id>
export GSHEET_TAB=hwbench

uvicorn main:app --host 127.0.0.1 --port 8000 --reload
```

Health check:
```bash
curl http://localhost:8000/health
```

Submit a result:
```bash
curl -X POST http://localhost:8000/submit \
  -H "Content-Type: application/json" \
  -d @../results/benchmark_results_*.json
```

List leaderboard:
```bash
curl http://localhost:8000/results
```
