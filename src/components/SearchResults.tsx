"use client"

import * as React from "react"
import { FileText, Hash, BarChart3 } from "lucide-react"
import { Card, CardHeader, CardTitle, CardContent } from "@/components/ui/card"
import { SearchResult } from "@/lib/api"
import { cn } from "@/lib/utils"

interface SearchResultsProps {
    results: SearchResult[]
    query: string
}

export function SearchResults({ results, query }: SearchResultsProps) {
    if (!query) return null

    // Safety check just in case
    if (!Array.isArray(results)) {
        return null;
    }

    if (results.length === 0) {
        return (
            <div className="text-center py-12 text-muted-foreground animate-in fade-in zoom-in-95">
                <p className="text-lg">No results found for "{query}"</p>
                <p className="text-sm mt-2 opacity-70">Try varying your sentence query.</p>
            </div>
        )
    }

    return (
        <div className="space-y-4 w-full max-w-4xl mx-auto mt-8">
            <div className="flex justify-between items-center text-sm text-muted-foreground px-2">
                <span>Found {results.length} matches</span>
                <span>Sentence Search</span>
            </div>
            <div className="grid grid-cols-1 sm:grid-cols-2 md:grid-cols-3 gap-4">
                {results.map((result, idx) => (
                    <ResultItem key={idx} result={result} />
                ))}
            </div>
        </div>
    )
}

function ResultItem({ result }: { result: SearchResult }) {
    return (
        <Card className="hover:shadow-md transition-all border-l-4 border-l-primary/50 hover:border-l-primary group">
            <CardHeader className="p-4 pb-2">
                <CardTitle className="text-base font-medium flex items-center gap-2">
                    <FileText className="h-4 w-4 text-primary" />
                    File ID: {result.file_id}
                </CardTitle>
            </CardHeader>
            <CardContent className="p-4 pt-2 space-y-2">
                <div className="flex items-center text-sm text-muted-foreground">
                    <Hash className="h-3.5 w-3.5 mr-2 opacity-70" />
                    <span>Sentence ID: {result.sentence_id}</span>
                </div>
                <div className="flex items-center text-sm font-medium text-foreground/80">
                    <BarChart3 className="h-3.5 w-3.5 mr-2 text-green-500" />
                    <span>Frequency: {result.frequency}</span>
                </div>
            </CardContent>
        </Card>
    )
}
