import { SearchHit, SearchResponse } from "@/lib/api";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { FileText, BookOpen } from "lucide-react";

interface SearchResultsProps {
    results: SearchResponse;
    query: string;
}

interface GroupedResult {
    file_id: number;
    filename: string;
    matches: SearchHit[];
}

export function SearchResults({ results, query }: SearchResultsProps) {
    if (!query) return null;

    // Safety check: ensure results.hits exists
    if (!results || !results.hits) {
        return null;
    }

    if (results.total_hits === 0) {
        return (
            <div className="text-center text-muted-foreground py-8 animate-in fade-in zoom-in-95">
                <p className="text-lg">No results found for "{query}"</p>
                <p className="text-sm mt-2 opacity-70">Try varying your sentence query.</p>
            </div>
        );
    }

    // Group by file_id safely
    const groups: Record<number, GroupedResult> = {};

    results.hits.forEach(hit => {
        if (!groups[hit.file_id]) {
            groups[hit.file_id] = {
                file_id: hit.file_id,
                filename: hit.filename,
                matches: []
            };
        }
        groups[hit.file_id].matches.push(hit);
    });

    const grouped = Object.values(groups);

    return (
        <div className="space-y-6 w-full max-w-4xl mx-auto mt-8">
            <h2 className="text-xl font-semibold mb-4">
                Found {results.total_hits} matches in {grouped.length} files
            </h2>

            {grouped.map((group) => (
                <Card key={group.file_id} className="overflow-hidden">
                    <CardHeader className="bg-muted/30 pb-3">
                        <CardTitle className="text-base font-medium flex items-center gap-2">
                            <FileText className="h-4 w-4 text-primary" />
                            {group.filename}
                            <span className="text-sm font-normal text-muted-foreground ml-auto">
                                {group.matches.length} matches
                            </span>
                        </CardTitle>
                    </CardHeader>
                    <CardContent className="pt-4">
                        <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-3">
                            {group.matches.map((match, idx) => (
                                <div key={idx} className="flex items-center gap-3 p-2 rounded-md border bg-card hover:bg-accent/50 transition-colors">
                                    <BookOpen className="h-4 w-4 text-muted-foreground" />
                                    <div className="flex flex-col overflow-hidden min-w-0">
                                        <div className="flex items-center gap-2">
                                            <span className="text-sm font-medium">Page {match.page_number}</span>
                                            <span className="text-xs text-muted-foreground">• Sent #{match.sentence_id}</span>
                                        </div>
                                        <p className="text-xs text-muted-foreground truncate italic">
                                            {match.sentence_text || "Match found"}
                                        </p>
                                    </div>
                                    <div className="ml-auto text-xs font-mono bg-primary/10 text-primary px-2 py-1 rounded-full shrink-0">
                                        {match.frequency}x
                                    </div>
                                </div>
                            ))}
                        </div>
                    </CardContent>
                </Card>
            ))}
        </div>
    );
}
