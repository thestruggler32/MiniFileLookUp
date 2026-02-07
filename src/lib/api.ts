export interface FileMetadata {
    file_id: number;
    filename: string;
    size_bytes: number;
    word_count: number;
    sentence_count: number;
    page_count: number;
}

export interface IndexResponse {
    success: boolean;
    indexed_files: FileMetadata[];
    total_files: number;
}

export interface SearchHit {
    file_id: number;
    filename: string;
    page_number: number;
    sentence_id: number;
    sentence_text: string;
    frequency: number;
}

export interface SearchResponse {
    query: string;
    total_hits: number;
    hits: SearchHit[];
}

const BASE_URL = "http://127.0.0.1:8000";

export async function search(query: string): Promise<SearchResponse> {
    if (!query) return { query: "", total_hits: 0, hits: [] };
    try {
        const params = new URLSearchParams({ query });
        const res = await fetch(`${BASE_URL}/search?${params.toString()}`);
        if (!res.ok) throw new Error("Search failed");
        return await res.json();
    } catch (e) {
        console.error(e);
        return { query, total_hits: 0, hits: [] };
    }
}

export async function autocomplete(prefix: string): Promise<string[]> {
    if (!prefix) return [];
    try {
        const params = new URLSearchParams({ prefix });
        const res = await fetch(`${BASE_URL}/autocomplete?${params}`);
        if (!res.ok) return [];
        return await res.json();
    } catch (e) {
        console.error(e);
        return [];
    }
}

export async function indexFiles(files: File[]): Promise<IndexResponse> {
    const formData = new FormData();
    files.forEach(file => formData.append('files', file));

    try {
        const res = await fetch(`${BASE_URL}/index`, {
            method: 'POST',
            body: formData,
        });

        if (!res.ok) throw new Error('Indexing failed');
        return await res.json();
    } catch (e) {
        console.error(e);
        throw e;
    }
}

export async function checkHealth(): Promise<boolean> {
    try {
        const res = await fetch(`${BASE_URL}/health`);
        return res.ok;
    } catch (e) {
        return false;
    }
}

export async function listFiles(): Promise<FileMetadata[]> {
    try {
        const res = await fetch(`${BASE_URL}/files`);
        if (!res.ok) return [];
        return await res.json();
    } catch (e) {
        console.error(e);
        return [];
    }
}
