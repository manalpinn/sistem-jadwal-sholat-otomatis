function showPage(id) {
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.getElementById(id).classList.add('active');
}

async function fetchJSON(url, options={}, timeout=8000) {
  const ctrl = new AbortController();
  setTimeout(() => ctrl.abort(), timeout);
  const res = await fetch(url, { ...options, signal: ctrl.signal });
  if (!res.ok) throw new Error();
  return res.json();
}

/* ================= DASHBOARD ================= */
async function loadStatus() {
  try {
    const s = await fetchJSON('/api/status');
    document.getElementById('statusBox').innerHTML = `
      <b>Kota:</b> ${s.kota}<br>
      <b>Waktu:</b> ${s.waktu}<br>
      <b>WiFi:</b> ${s.wifi}
    `;
    if (s.mode === 'AP') {
      document.getElementById('apBanner').classList.remove('hidden');
    }
  } catch {
    document.getElementById('statusBox').textContent = 'ESP32 tidak merespon.';
  }
}

async function loadJadwalToday() {
  const table = document.getElementById('jadwalTable');

  table.innerHTML = `
    <tr><td colspan="2">⏳ Memuat jadwal hari ini...</td></tr>
  `;

  try {
    const res = await fetch('/api/jadwal_today');
    const d = await res.json();

    console.log('Jadwal hari ini:', d);

    if (!res.ok || d.error) {
      throw new Error(d.error || 'jadwal tidak tersedia');
    }

    table.innerHTML = `
      <tr><th>Waktu</th><th>Jam</th></tr>
      <tr><td>Imsak</td><td>${d.imsak}</td></tr>
      <tr><td>Subuh</td><td>${d.subuh}</td></tr>
      <tr><td>Dzuhur</td><td>${d.dzuhur}</td></tr>
      <tr><td>Ashar</td><td>${d.ashar}</td></tr>
      <tr><td>Maghrib</td><td>${d.maghrib}</td></tr>
      <tr><td>Isya</td><td>${d.isya}</td></tr>
    `;
  } catch (e) {
    console.error(e);
    table.innerHTML = `
      <tr><td colspan="2">❌ Jadwal belum tersedia</td></tr>
    `;
  }
}


/* ================= KOTA ================= */
async function searchKota() {
  const q = kotaInput.value.trim();
  kotaList.innerHTML = '';
  kotaMessage.textContent = 'Mencari kota...';

  if (!q) return kotaMessage.textContent = 'Masukkan nama kota.';

  try {
    const r = await fetchJSON(`/api/search_kota?kota=${q}`);
    const d = r.data || r;
    if (!d.length) {
      kotaMessage.textContent = '❌ Kota tidak tersedia.';
      return;
    }
    kotaMessage.textContent = '';
    d.forEach(k => {
      const li = document.createElement('li');
      li.textContent = k.lokasi;
      li.onclick = () => setKota(k.id, k.lokasi);
      kotaList.appendChild(li);
    });
  } catch {
    kotaMessage.textContent = 'WiFi tidak tersedia.';
  }
}

async function setKota(id, nama) {
  console.log('setKota dipanggil:', { id, nama });

  if (!id || !nama) {
    kotaMessage.textContent = '❌ Data kota tidak valid.';
    return;
  }

  kotaMessage.textContent = '⏳ Menyimpan kota & mengunduh jadwal...';

  try {
    const res = await fetch('/api/set_kota', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        id: Number(id),   // 🔴 PENTING: pastikan number
        nama: nama
      })
    });

    const data = await res.json();
    console.log('Response set_kota:', data);

    if (!data.success) {
      kotaMessage.textContent = '❌ ' + data.error;
      return;
    }

    kotaMessage.textContent = '✅ Kota tersimpan. Memuat jadwal...';

    // 🔥 AUTO REFRESH JADWAL
    await loadJadwalToday();

  } catch (e) {
    console.error(e);
    kotaMessage.textContent = '❌ Gagal terhubung ke ESP32';
  }
}


/* ================= WIFI ================= */
async function scanWifi() {
  wifiList.innerHTML = 'Scanning...';

  try {
    const res = await fetch('/api/wifi/scan');
    if (!res.ok) throw new Error('Scan gagal, status ' + res.status);
    const data = await res.json(); // parse JSON dari ESP32

    wifiList.innerHTML = '';
    data.forEach(w => {
      const li = document.createElement('li');
      li.textContent = `${w.ssid} (${w.rssi} dBm)`;
      li.onclick = () => ssid.value = w.ssid;
      wifiList.appendChild(li);
    });

    if (data.length === 0) {
      wifiList.innerHTML = '<li>No networks found</li>';
    }

  } catch (err) {
    console.error(err);
    wifiStatus.textContent = 'Scan gagal.';
    wifiList.innerHTML = '';
  }
}

async function connectWifi() {
  wifiStatus.textContent = 'Menghubungkan...';
  try {
    const r = await fetchJSON('/api/wifi/connect', {
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body: JSON.stringify({
        ssid:ssid.value,
        password:password.value
      })
    }, 15000);
    wifiStatus.textContent = r.success ? '✅ WiFi terhubung.' : '❌ Gagal.';
  } catch {
    wifiStatus.textContent = 'Gagal terhubung atau ESP32 tidak merespon.';
  }
}


function runAdzanNow() {
  const select = document.getElementById("adzanIndex");
  const index = parseInt(select.value, 10);

  const namaSholat = ["Subuh","Dzuhur","Ashar","Maghrib","Isya"];

  document.getElementById("adzanTestMessage").innerText =
    "⏳ Menjalankan adzan...";
  document.getElementById("adzanTestMessage").className = "message";

  fetch("/api/adzan/instant", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ index })
  })
  .then(res => {
    if (!res.ok) throw new Error("ESP error");
    return res.json();
  })
  .then(data => {
    if (data.success) {
      document.getElementById("adzanTestMessage").innerText =
        `🔊 Adzan ${namaSholat[index]} dijalankan`;
      document.getElementById("adzanTestMessage").className =
        "message success";
    } else {
      throw new Error(data.error || "Gagal");
    }
  })
  .catch(err => {
    console.error(err);
    document.getElementById("adzanTestMessage").innerText =
      "❌ ESP32 tidak merespon adzan";
    document.getElementById("adzanTestMessage").className =
      "message error";
  });
}

/* ================= SETTINGS ================= */
document.addEventListener('DOMContentLoaded', async () => {
  const volumeSlider = document.getElementById('volume');
  const volumeValue = document.getElementById('volumeValue');
  const configMessage = document.getElementById('configMessage');
  const saveBtn = document.getElementById('saveBtn');
  const buzzer = document.getElementById('buzzer');

  // ✅ Checkbox default true saat halaman load
  buzzer.checked = true;

  // Ambil config dari ESP32 (jika ada) dan update slider & checkbox
  try {
    const cfg = await fetch('/api/config').then(r => r.json());
    if (cfg.volume !== undefined) {
      volumeSlider.value = cfg.volume;
      volumeValue.textContent = cfg.volume;
    }
    if (cfg.buzzer !== undefined) {
      buzzer.checked = cfg.buzzer; // update sesuai config ESP32
    }
  } catch {
    console.warn("Tidak bisa mengambil config awal dari ESP32");
  }

  // Update slider realtime
  volumeSlider.addEventListener('input', () => {
    let val = parseInt(volumeSlider.value);
    if (val > 25) val = 25;
    volumeValue.textContent = val;
  });

  // Save config ke ESP32
  saveBtn.addEventListener('click', async () => {
    let vol = parseInt(volumeSlider.value);
    if (isNaN(vol) || vol < 0) vol = 0;
    if (vol > 25) vol = 25;

    configMessage.textContent = '⏳ Menyimpan pengaturan...';
    configMessage.style.color = 'black';

    try {
      const r = await fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          volume: vol,
          buzzer: buzzer.checked // ✅ kirim status checkbox saat ini
        })
      });

      const data = await r.json();
      if (data.success) {
        configMessage.textContent = '✅ Pengaturan berhasil disimpan!';
        configMessage.style.color = 'green';
        setTimeout(() => configMessage.textContent = '', 3000);
      } else {
        configMessage.textContent = '❌ Tidak bisa menyimpan konfigurasi. Periksa ESP32.';
        configMessage.style.color = 'red';
      }

    } catch (err) {
      console.error(err);
      configMessage.textContent = '❌ Tidak bisa terhubung ke ESP32. Periksa koneksi atau server.';
      configMessage.style.color = 'red';
    }
  });
});



/* INIT */
loadStatus();
loadJadwalToday();
setInterval(loadStatus, 10000);
document.addEventListener('DOMContentLoaded', loadJadwalToday);
