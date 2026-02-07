import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { FileMetadata, listFiles } from "@/lib/api";
import { FileText, Database } from "lucide-react";
import { useEffect, useState } from "react";

function formatBytes(bytes: number, decimals = 2) {
    if (!+bytes) return '0 Bytes';
    const k = 1024;
    const dm = decimals < 0 ? 0 : decimals;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return `${parseFloat((bytes / Math.pow(k, i)).toFixed(dm))} ${sizes[i]}`;
}

export function IndexedFiles() {
    const [files, setFiles] = useState<FileMetadata[]>([]);
    const [hasLoaded, setHasLoaded] = useState(false);

    useEffect(() => {
        async function fetch() {
            try {
                const data = await listFiles();
                setFiles(data);
                setHasLoaded(true);
            } catch (e) {
                console.error("Failed to fetch files", e);
            }
        }
        fetch();
        const interval = setInterval(fetch, 10000); // Poll every 10s (reduced flicker)
        return () => clearInterval(interval);
    }, []);

    // Only hide before first load, not during refreshes
    if (!hasLoaded) return null;

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
                    {files.map((file) => (
                        <div key={file.file_id} className="flex items-start gap-3 border-l-2 border-primary/20 pl-3 py-1 hover:bg-muted/50 rounded-r transition-colors">
                            <FileText className="h-4 w-4 text-muted-foreground mt-1" />
                            <div>
                                <p className="font-medium text-sm leading-none mb-1">{file.filename}</p>
                                <p className="text-xs text-muted-foreground">
                                    {formatBytes(file.size_bytes)} • {file.page_count} pages • {file.word_count} words
                                </p>
                            </div>
                        </div>
                    ))}
                </div>
            </CardContent>
        </Card>
    );
}
