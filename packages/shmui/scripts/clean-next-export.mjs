#!/usr/bin/env node
import { rm, rename } from "node:fs/promises"
import path from "node:path"
import process from "node:process"
import { setTimeout as delay } from "node:timers/promises"

const LOG_PREFIX = "[clean-next-export]"
const MAX_ATTEMPTS = Number(process.env.CLEAN_RETRY_COUNT ?? 5)
const BASE_DELAY_MS = Number(process.env.CLEAN_RETRY_DELAY_MS ?? 100)

const targetArg = process.argv[2]
const targetPath = path.resolve(process.cwd(), targetArg ?? "packages/shmui/apps/www/.next/export")

const log = (message) => console.log(`${LOG_PREFIX} ${message}`)
const warn = (message) => console.warn(`${LOG_PREFIX} ${message}`)

log(`Preparing to remove \"${targetPath}\"`)

async function removePath(attempt, candidate) {
  try {
    await rm(candidate, { recursive: true, force: true })
    log(`Removal succeeded on attempt ${attempt} for \"${candidate}\"`)
    return true
  } catch (error) {
    warn(`Attempt ${attempt} failed for \"${candidate}\": ${error.code ?? error.message}`)
    return false
  }
}

async function tryWithRetries(candidate, reason) {
  for (let attempt = 1; attempt <= MAX_ATTEMPTS; attempt += 1) {
    if (await removePath(attempt, candidate)) {
      return true
    }
    const delayMs = BASE_DELAY_MS * 2 ** (attempt - 1)
    log(`${reason}: retrying in ${delayMs}ms (attempt ${attempt + 1} of ${MAX_ATTEMPTS})`)
    await delay(delayMs)
  }
  return false
}

let cleaned = await tryWithRetries(targetPath, "Primary removal")

if (!cleaned) {
  const fallbackPath = `${targetPath}-${Date.now()}-stale`
  try {
    await rename(targetPath, fallbackPath)
    log(`Renamed stubborn path to \"${fallbackPath}\" for deferred cleanup`)
    cleaned = await tryWithRetries(fallbackPath, "Fallback removal")
  } catch (error) {
    if (error.code === "ENOENT") {
      log(`Path \"${targetPath}\" no longer exists; nothing to clean`)
      cleaned = true
    } else {
      warn(`Unable to rename \"${targetPath}\" for fallback cleanup: ${error.code ?? error.message}`)
    }
  }
}

if (cleaned) {
  log(`Cleanup complete for \"${targetPath}\"`)
} else {
  warn(`Cleanup did not fully succeed for \"${targetPath}\"; continuing without failure`)
}

process.exit(0)
