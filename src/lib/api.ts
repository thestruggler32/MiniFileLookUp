
export interface SearchResult {
    file_id: number;
    sentence_id: number;
    frequency: number;
}

export interface IndexResponse {
    indexed_files: number;
}

const BASE_URL = "http://127.0.0.1:8000";

export async function search(query: string): Promise<SearchResult[]> {
    if (!query) return [];
    try {
        // Use URLSearchParams for safe encoding
        const params = new URLSearchParams({ query });
        const res = await fetch(`${BASE_URL}/search?${params.toString()}`);
        if (!res.ok) throw new Error("Search failed");
        return await res.json();
    } catch (e) {
        console.error(e);
        return [];
    }
}

export async function autocomplete(prefix: string): Promise<string[]> {
    if (!prefix) return [];
    try {
        const params = new URLSearchParams({ prefix });
        const res = await fetch(`${BASE_URL}/autocomplete?${params.toString()}`);
        if (!res.ok) throw new Error("Autocomplete failed");
        return await res.json();
    } catch (e) {
        console.error(e);
        return [];
    }
}

export async function indexFiles(files: File[]): Promise<IndexResponse> {
    const formData = new FormData();
    files.forEach(file => {
        formData.append("files", file);
    });

    try {
        const res = await fetch(`${BASE_URL}/index`, {
            method: "POST",
            body: formData,
        });
        if (!res.ok) throw new Error("Indexing failed");
        return await res.json();
    } catch (e) {
        console.error(e);
        throw e;
    }
}

export async function checkHealth(): Promise<boolean> {
    try {
        const res = await fetch(`${BASE_URL}/health`);
        const json = await res.json();
        return json.status === "ok";
    } catch (e) {
        return false;
    }
}
