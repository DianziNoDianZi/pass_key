import Database from 'better-sqlite3';
import path from 'path';
import { SCHEMA_SQL } from './schema';

let db: Database.Database;

export function getDatabase(dbPath?: string): Database.Database {
  if (!db) {
    const resolvedPath = dbPath || path.join(__dirname, '../../data/passkey.db');

    // Ensure directory exists
    const dir = path.dirname(resolvedPath);
    const fs = require('fs');
    if (!fs.existsSync(dir)) {
      fs.mkdirSync(dir, { recursive: true });
    }

    db = new Database(resolvedPath);
    db.pragma('journal_mode = WAL');
    db.pragma('foreign_keys = ON');

    // Initialize schema
    db.exec(SCHEMA_SQL);
  }
  return db;
}

export function closeDatabase(): void {
  if (db) {
    db.close();
  }
}
