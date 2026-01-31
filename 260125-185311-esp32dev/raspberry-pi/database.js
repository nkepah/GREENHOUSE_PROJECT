const sqlite3 = require('sqlite3').verbose();
const path = require('path');

const DB_PATH = path.join(__dirname, 'farm.db');

const db = new sqlite3.Database(DB_PATH, (err) => {
    if (err) {
        console.error('[DB] Error opening database:', err.message);
    } else {
        console.log('[DB] Connected to SQLite database.');
        initSchema();
    }
});

function initSchema() {
    db.run(`CREATE TABLE IF NOT EXISTS settings (
        key TEXT PRIMARY KEY,
        value TEXT
    )`, (err) => {
        if (err) {
            console.error('[DB] Error creating table:', err.message);
        } else {
            // Seed default settings if empty
            db.get("SELECT count(*) as count FROM settings", (err, row) => {
                if(row && row.count === 0) {
                   seedDefaults();
                }
            });
        }
    });
    
    // Create weather cache table with both C and F temperatures
    db.run(`CREATE TABLE IF NOT EXISTS weather_cache (
        id INTEGER PRIMARY KEY,
        timestamp INTEGER,
        current_temp_c REAL,
        current_temp_f REAL,
        weather_code INTEGER,
        daily_data TEXT,
        hourly_data TEXT,
        timezone TEXT DEFAULT 'UTC'
    )`, (err) => {
        if (err) {
            console.error('[DB] Error creating weather_cache table:', err.message);
        }
    });
}

function seedDefaults() {
    const now = new Date();
    const timestamp = Math.floor(now.getTime() / 1000);
    
    const defaults = {
        'farm_name': 'Smart Farm Hub',
        'theme': 'default',
        'units': JSON.stringify({temp: 'C', speed: 'km/h', pressure: 'hpa', date: 'DD/MM/YYYY'}),
        'location': JSON.stringify({lat: -17.8292, lon: 31.0522, timezone: 'Africa/Harare', address: 'Harare, Zimbabwe'}),
        'systemTime': JSON.stringify({unix: timestamp, iso: now.toISOString(), synced: false, source: 'server'})
    };
    
    const stmt = db.prepare("INSERT INTO settings (key, value) VALUES (?, ?)");
    Object.keys(defaults).forEach(key => {
        stmt.run(key, defaults[key]);
    });
    stmt.finalize();
    console.log('[DB] seeded default settings');
}

module.exports = {
    get: (key) => {
        return new Promise((resolve, reject) => {
            db.get("SELECT value FROM settings WHERE key = ?", [key], (err, row) => {
                if (err) reject(err);
                else resolve(row ? row.value : null);
            });
        });
    },
    
    set: (key, value) => {
        return new Promise((resolve, reject) => {
            // value should be string. If object, stringify it before calling this, or we handle it here.
            // Let's assume caller handles stringification for complex objects to keep DB simple key-value
            if (typeof value !== 'string') value = JSON.stringify(value);
            
            db.run("INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = ?", 
                [key, value, value], (err) => {
                if (err) reject(err);
                else resolve(true);
            });
        });
    },
    
    getAll: () => {
        return new Promise((resolve, reject) => {
            db.all("SELECT key, value FROM settings", [], (err, rows) => {
                if (err) reject(err);
                else {
                    const settings = {};
                    rows.forEach(row => {
                        try {
                            // Try to parse JSON values automatically
                            settings[row.key] = JSON.parse(row.value);
                        } catch (e) {
                            settings[row.key] = row.value;
                        }
                    });
                    resolve(settings);
                }
            });
        });
    },
    
    // Weather cache functions - store pre-converted data
    saveWeatherCache: (current_temp_c, current_temp_f, weather_code, daily_data, hourly_data, timezone = 'UTC') => {
        return new Promise((resolve, reject) => {
            const timestamp = Math.floor(Date.now() / 1000);
            
            // Delete old cache entries (keep only latest)
            db.run("DELETE FROM weather_cache", [], (err) => {
                if (err) {
                    console.error('[DB] Error clearing old weather cache:', err);
                    reject(err);
                    return;
                }
                
                // Insert new weather data with both C and F conversions
                db.run(
                    "INSERT INTO weather_cache (timestamp, current_temp_c, current_temp_f, weather_code, daily_data, hourly_data, timezone) VALUES (?, ?, ?, ?, ?, ?, ?)",
                    [timestamp, current_temp_c, current_temp_f, weather_code, daily_data, hourly_data, timezone],
                    (err) => {
                        if (err) reject(err);
                        else resolve(true);
                    }
                );
            });
        });
    }
    
    getWeatherCache: () => {
        return new Promise((resolve, reject) => {
            db.get("SELECT * FROM weather_cache ORDER BY timestamp DESC LIMIT 1", [], (err, row) => {
                if (err) reject(err);
                else resolve(row || null);
            });
        });
    }
};

