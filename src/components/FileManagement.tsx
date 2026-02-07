"use client"

import * as React from "react"
import { Upload, FileText, Database, CheckCircle2, AlertCircle } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Card, CardContent } from "@/components/ui/card"
import { indexFiles, checkHealth } from "@/lib/api"
import { cn } from "@/lib/utils"

export function FileManagement() {
    const [isIndexing, setIsIndexing] = React.useState(false)
    const [indexedCount, setIndexedCount] = React.useState<number | null>(null)
    const [status, setStatus] = React.useState<"idle" | "success" | "error">("idle")
    const [isBackendHealthy, setIsBackendHealthy] = React.useState(true)
    const fileInputRef = React.useRef<HTMLInputElement>(null)

    React.useEffect(() => {
        checkHealth().then(healthy => setIsBackendHealthy(healthy))
    }, [])

    const handleFileSelect = async (e: React.ChangeEvent<HTMLInputElement>) => {
        if (e.target.files && e.target.files.length > 0) {
            setIsIndexing(true)
            setStatus("idle")
            try {
                const res = await indexFiles(Array.from(e.target.files))
                setIndexedCount(res.indexed_files)
                setStatus("success")
            } catch (error) {
                console.error("Indexing failed", error)
                setStatus("error")
            } finally {
                setIsIndexing(false)
                // Reset file input
                if (fileInputRef.current) fileInputRef.current.value = ""
            }
        }
    }

    return (
        <div className="w-full max-w-2xl mx-auto mb-8">
            <div className="flex items-center justify-between mb-2 px-2">
                <h2 className="text-sm font-medium text-muted-foreground uppercase tracking-wide">Index Management</h2>
                {!isBackendHealthy && (
                    <span className="flex items-center text-xs text-destructive animate-pulse">
                        <AlertCircle className="h-3 w-3 mr-1" /> Backend Offline
                    </span>
                )}
            </div>

            <Card className="border-dashed bg-muted/20 hover:bg-muted/30 transition-colors">
                <CardContent className="flex items-center justify-between p-4">
                    <div className="flex items-center space-x-4">
                        <div className={cn("p-2 rounded-full transition-all",
                            isIndexing ? "bg-primary/10 animate-pulse" :
                                status === "success" ? "bg-green-500/10" :
                                    status === "error" ? "bg-destructive/10" : "bg-primary/10"
                        )}>
                            {isIndexing ? <Database className="h-5 w-5 text-primary animate-spin" /> :
                                status === "success" ? <CheckCircle2 className="h-5 w-5 text-green-500" /> :
                                    status === "error" ? <AlertCircle className="h-5 w-5 text-destructive" /> :
                                        <Database className="h-5 w-5 text-primary" />}
                        </div>
                        <div>
                            <p className="font-medium">
                                {status === "success" ? `Successfully indexed ${indexedCount} files` :
                                    status === "error" ? "Indexing failed" : "Local Index"}
                            </p>
                            <p className="text-xs text-muted-foreground">
                                {isIndexing ? "Processing files..." : "Supports .txt, .pdf, .docx, .csv"}
                            </p>
                        </div>
                    </div>

                    <div className="flex items-center space-x-2">
                        <input
                            type="file"
                            multiple
                            className="hidden"
                            ref={fileInputRef}
                            onChange={handleFileSelect}
                            accept=".txt,.pdf,.csv,.docx"
                        />
                        <Button
                            size="sm"
                            variant="outline"
                            disabled={isIndexing || !isBackendHealthy}
                            onClick={() => fileInputRef.current?.click()}
                            className="gap-2"
                        >
                            <Upload className="h-3.5 w-3.5" />
                            {isIndexing ? "Indexing..." : "Upload Files"}
                        </Button>
                    </div>
                </CardContent>
            </Card>
        </div>
    )
}
