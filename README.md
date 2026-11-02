# @vencord/game-detection

Native Node-API addon that matches running processes against Discord's detectable games database. Supports Linux (including games running through Wine and Proton), Windows 10+ and macOS, on amd64 and arm64.

## Usage

```js
const { setDetectableDatabase, getRunningGames } = require("@vencord/game-detection");

const games = await fetch("https://discord.com/api/v9/applications/detectable").then(r => r.json());
setDetectableDatabase(games);

setInterval(async () => {
    console.log(await getRunningGames());
    // [{ id: "363411421553360896", name: "Last Man Standing", pid: 1234, path: "c:/games/lms/lms.exe" }]
}, 5000);
```

`getRunningGames` runs on a worker thread, so it does not block the event loop.

## Building

```sh
pnpm install
pnpm build
```

### Linux Games

Games ran through Proton/Wine are matched against the Windows entries

For games with Linux-native versions, most games in the database only include Windows executables, so they would not be detected. Instead, they are matched by their Steam app id

Native Linux games from non-steam sources can only be found if the database has a `linux` entry for them.

### Minecraft

Discord's database has no Linux entries for Java games (e.g. Minecraft), so macOS entries for them are used on Linux too.

The database only has vanilla Minecraft. To match alternative launchers & mod loaders, they are hardcoded in `src/matcher.cc`. This way, Prism, Fabric, etc. should all be detected as well.

### Game not detected?

Open an issue with as many details as possible:
- The name of the game
- The platform you are running on
- The full command line of the game process
