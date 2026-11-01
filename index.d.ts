export interface DetectableExecutable {
    name: string;
    os: string;
    is_launcher?: boolean;
    arguments?: string | null;
}

export interface DetectableGame {
    id: string;
    name: string;
    executables?: DetectableExecutable[] | null;
    third_party_skus?: { distributor: string; id?: string | null }[] | null;
}

export interface RunningGame {
    id: string;
    name: string;
    pid: number;
    /* Lowercased with forward slashes */
    path: string;
}

export function setDetectableDatabase(games: DetectableGame[]): void;

export function getRunningGames(): Promise<RunningGame[]>;
