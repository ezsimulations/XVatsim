const { chromium } = require("playwright");
const { pathToFileURL } = require("url");
const path = require("path");
const fs = require("fs");

async function main() {
  const input = process.argv[2];
  const output = process.argv[3];
  if (!input || !output) {
    throw new Error("usage: node render-map.cjs <map.html> <screenshot.png>");
  }
  const browserCandidates = [
    "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
    "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
    "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
    "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
  ];
  const executablePath = browserCandidates.find((candidate) => fs.existsSync(candidate));
  const browser = await chromium.launch({ headless: true, executablePath });
  try {
    const page = await browser.newPage({ viewport: { width: 1800, height: 1100 } });
    await page.goto(pathToFileURL(path.resolve(input)).href, {
      waitUntil: "networkidle",
      timeout: 60000,
    });
    await page.screenshot({ path: path.resolve(output), fullPage: false });
  } finally {
    await browser.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
