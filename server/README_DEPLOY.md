# hwbench Relay Server — Deployment Guide

## 1. Google Drive Setup

### 1.1 Create a Google Cloud Project
1. Go to [console.cloud.google.com](https://console.cloud.google.com).
2. Click **Select a project** → **New Project**.
3. Name it `hwbench` (or anything you like) and click **Create**.
4. Make sure this project is selected in the top bar.

### 1.2 Enable Google Drive API
1. In the left sidebar go to **APIs & Services → Library**.
2. Search for `Google Drive API` and click it.
3. Click **Enable**.

### 1.3 Create a Service Account
1. Go to **APIs & Services → Credentials**.
2. Click **+ Create Credentials → Service Account**.
3. Name it `hwbench-uploader`, click **Create and Continue**.
4. Skip the optional role and user-access steps; click **Done**.
5. Click the service account email you just created.
6. Go to the **Keys** tab → **Add Key → Create New Key → JSON**.
7. A `.json` file downloads — keep it safe. You will set its path as `GOOGLE_SERVICE_ACCOUNT_JSON`.

### 1.4 Create a Google Drive folder
1. Open [drive.google.com](https://drive.google.com).
2. Click **+ New → New Folder**, name it `hwbench-results`.
3. Right-click the folder → **Share**.
4. Paste the service account email (ends with `@...iam.gserviceaccount.com`) in the **Add people** box.
5. Set role to **Editor**, click **Send**.
6. Get the folder ID: open the folder, copy the last segment of the URL:
   ```
   https://drive.google.com/drive/folders/1AbCdEfGhIjKlMnOpQrStUvWxYz
                                           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
                                           This is your GDRIVE_FOLDER_ID
   ```

---

## 2. Deploy on Railway (recommended free tier)

### 2.1 Install Railway CLI
```bash
npm install -g @railway/cli   # or brew install railway
```

### 2.2 Deploy
```bash
cd server/
railway login
railway init          # choose "Empty Project"
railway up            # deploys from current directory
```

### 2.3 Set Environment Variables in Railway Dashboard
1. Open [railway.app](https://railway.app) → your project → **Variables**.
2. Add:
   | Variable                    | Value                              |
   |-----------------------------|------------------------------------|
   | `GOOGLE_SERVICE_ACCOUNT_JSON` | `/app/service-account.json`      |
   | `GDRIVE_FOLDER_ID`          | your folder ID from step 1.4       |
3. Upload your service account JSON via Railway's **File Mount** or embed the
   JSON content directly as the variable value (see note below).

> **Tip — embed JSON as env var:** instead of a file path, you can serialize
> the JSON key file contents as a single-line string and set it as
> `GOOGLE_SERVICE_ACCOUNT_JSON_CONTENT`. Update `_get_drive_service()` in
> `main.py` to parse it with `service_account.Credentials.from_service_account_info(json.loads(...))`.

### 2.4 Get public URL
Railway assigns a URL like `https://hwbench-XXXX.up.railway.app`.
Update `RELAY_URL` at the top of `run.sh` with this URL + `/submit`.

---

## 3. Deploy on Render (free tier alternative)

1. Create account at [render.com](https://render.com).
2. Click **New → Web Service** → connect your GitHub repo.
3. Set:
   - **Root Directory**: `server`
   - **Build Command**: `pip install -r requirements.txt`
   - **Start Command**: `uvicorn main:app --host 0.0.0.0 --port $PORT`
4. Under **Environment**, add:
   - `GOOGLE_SERVICE_ACCOUNT_JSON` = path to your uploaded key file
   - `GDRIVE_FOLDER_ID` = your folder ID
5. Click **Deploy**.
6. Get the public URL from the Render dashboard.
7. Update `RELAY_URL` in `run.sh`.

---

## 4. Local Testing

### 4.1 Install dependencies
```bash
cd server/
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
```

### 4.2 Run the server
```bash
export GOOGLE_SERVICE_ACCOUNT_JSON=/path/to/service-account.json
export GDRIVE_FOLDER_ID=your_folder_id

uvicorn main:app --host 127.0.0.1 --port 8000 --reload
```

### 4.3 Test with curl

Health check:
```bash
curl http://localhost:8000/health
# {"status":"ok","version":"1.0.0"}
```

Submit a result file:
```bash
curl -X POST http://localhost:8000/submit \
  -H "Content-Type: application/json" \
  -d @results/benchmark_results_2026-06-26T18-00-00.json
```

List results:
```bash
curl http://localhost:8000/results
```

### 4.4 Test upload from run.sh
```bash
# In run.sh, temporarily change RELAY_URL to http://localhost:8000/submit
bash run.sh
# Answer prompts, then choose "y" when asked to upload
```
