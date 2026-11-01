import { createRequire } from "node:module";

const { getRunningGames, setDetectableDatabase} = createRequire(import.meta.url)("./build/Release/rpc_native.node")

const games = await fetch("https://discord.com/api/v9/applications/detectable").then(r => r.json());
setDetectableDatabase(games);

setInterval(async () => {
    console.log(await getRunningGames());
}, 5000);
