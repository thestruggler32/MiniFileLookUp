import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { listFiles } from "@/lib/api";
import { FileText, Database } from "lucide-react";
import { useEffect, useRef } from "react";
import { useFiles } from "@/app/FileContext";

function formatBytes(bytes: number, decimals = 2) {
    if (!+bytes) return '0 Bytes';
    const k = 1024;
    const dm = decimals < 0 ? 0 : decimals;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return `${parseFloat((bytes / Math.pow(k, i)).toFixed(dm))} ${sizes[i]}`;
}

export function IndexedFiles() {
    // Use the shared FileContext — FileManagement updates it on upload,
    // so this panel reflects new uploads instantly without waiting for a poll.
    const { files, setFiles } = useFiles();
    const hasSeededRef = useRef(false);

    useEffect(() => {
        async function fetchAndSync() {
            try {
                const data = await listFiles();
                // Only update state when we actually got data back.
                // If the request fails or returns empty (e.g. network error from
                // another device), we keep whatever is already in context.
                if (data.length > 0) {
                    setFiles(data);
                } else if (!hasSeededRef.current) {
                    // First load and server really has no files yet — that's fine.
                    setFiles([]);
                }
                hasSeededRef.current = true;
            } catch {
                // Network error — don't clear the list, keep what we have.
                hasSeededRef.current = true;
            }
        }

        fetchAndSync(); // immediate on mount
        const interval = setInterval(fetchAndSync, 10_000); // poll every 10 s
        return () => clearInterval(interval);
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, []);

    if (!hasSeededRef.current && files.length === 0) return null;

    return (
        <Card>
            <CardHeader className="pb-3">
                <CardTitle className="text-lg flex items-center gap-2">
                    <Database className="h-5 w-5 text-primary" />
                    Indexed Files ({files.length})
                </CardTitle>
            </CardHeader>
            <CardContent>
                <div className="space-y-3 max-h-[300px] overflow-y-auto pr-2">
                    {files.length === 0 ? (
                        <p className="text-sm text-muted-foreground text-center py-4">
                            No files indexed yet. Upload a document to get started.
                        </p>
                    ) : (
                        files.map((file) => (
                            <div key={file.file_id} className="flex items-start gap-3 border-l-2 border-primary/20 pl-3 py-1 hover:bg-muted/50 rounded-r transition-colors">
                                <FileText className="h-4 w-4 text-muted-foreground mt-1" />
                                <div>
                                    <p className="font-medium text-sm leading-none mb-1">{file.filename}</p>
                                    <p className="text-xs text-muted-foreground">
                                        {formatBytes(file.size_bytes)} • {file.page_count} pages • {file.word_count} words
                                    </p>
                                </div>
                            </div>
                        ))
                    )}
                </div>
            </CardContent>
        </Card>
    );
}

