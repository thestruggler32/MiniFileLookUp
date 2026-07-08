"use client";

import { useEffect, useState } from "react";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { getTelemetry, TelemetryData } from "@/lib/api";
import { Activity, Database, Cpu, HardDrive } from "lucide-react";
import { useFiles } from "@/app/FileContext";

export function TelemetryDashboard() {
    const [telemetry, setTelemetry] = useState<TelemetryData | null>(null);
    const { files } = useFiles(); // Refetch telemetry when files change

    useEffect(() => {
        const fetchTelemetry = async () => {
            const data = await getTelemetry();
            if (data) setTelemetry(data);
        };
        fetchTelemetry();
        
        // Also poll every 15s to keep it fresh
        const interval = setInterval(fetchTelemetry, 15000);
        return () => clearInterval(interval);
    }, [files]);

    if (!telemetry || telemetry.corpus_size_bytes === 0) return null;

    const formatBytes = (bytes: number) => {
        if (bytes < 1024) return bytes + " B";
        else if (bytes < 1048576) return (bytes / 1024).toFixed(1) + " KB";
        else return (bytes / 1048576).toFixed(2) + " MB";
    };

    return (
        <Card className="w-full bg-gradient-to-br from-card to-muted/20 border-primary/20 shadow-lg mt-8 mb-8 animate-in fade-in slide-in-from-bottom-4">
            <CardHeader className="pb-3 border-b border-border/50 bg-muted/10">
                <CardTitle className="text-lg font-bold flex items-center gap-2 text-primary">
                    <Activity className="h-5 w-5" /> Engine Telemetry
                </CardTitle>
                <div className="text-sm text-muted-foreground mt-1.5">
                    Real-time memory profiling of the FM-Index compression engine
                </div>
            </CardHeader>
            <CardContent className="pt-6">
                <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
                    <div className="flex flex-col gap-1 p-4 rounded-xl bg-background border shadow-sm">
                        <div className="flex items-center gap-2 text-muted-foreground mb-2">
                            <Database className="h-4 w-4 text-blue-500" />
                            <span className="text-xs font-semibold uppercase tracking-wider">Corpus Size</span>
                        </div>
                        <span className="text-2xl font-black tabular-nums">{formatBytes(telemetry.corpus_size_bytes)}</span>
                        <span className="text-xs text-muted-foreground">Raw text data</span>
                    </div>

                    <div className="flex flex-col gap-1 p-4 rounded-xl bg-background border shadow-sm relative overflow-hidden">
                        <div className="absolute top-0 right-0 p-2 opacity-10 pointer-events-none">
                            <Cpu className="h-16 w-16" />
                        </div>
                        <div className="flex items-center gap-2 text-muted-foreground mb-2">
                            <Cpu className="h-4 w-4 text-emerald-500" />
                            <span className="text-xs font-semibold uppercase tracking-wider">Index RAM</span>
                        </div>
                        <span className="text-2xl font-black tabular-nums text-emerald-600 dark:text-emerald-400">
                            {formatBytes(telemetry.total_index_size_bytes)}
                        </span>
                        <span className="text-xs font-medium text-emerald-600/80 dark:text-emerald-400/80">
                            {telemetry.compression_ratio.toFixed(1)}% of original size
                        </span>
                    </div>

                    <div className="flex flex-col gap-1 p-4 rounded-xl bg-background border shadow-sm">
                        <div className="flex items-center gap-2 text-muted-foreground mb-2">
                            <HardDrive className="h-4 w-4 text-purple-500" />
                            <span className="text-xs font-semibold uppercase tracking-wider">BWT Size</span>
                        </div>
                        <span className="text-2xl font-black tabular-nums">{formatBytes(telemetry.bwt_size_bytes)}</span>
                        <span className="text-xs text-muted-foreground">Burrows-Wheeler Transform</span>
                    </div>

                    <div className="flex flex-col gap-1 p-4 rounded-xl bg-background border shadow-sm">
                        <div className="flex items-center gap-2 text-muted-foreground mb-2">
                            <Activity className="h-4 w-4 text-amber-500" />
                            <span className="text-xs font-semibold uppercase tracking-wider">Wavelet Tree</span>
                        </div>
                        <span className="text-2xl font-black tabular-nums">{formatBytes(telemetry.wm_size_bytes)}</span>
                        <span className="text-xs text-muted-foreground">O(1) Rank Bitvectors</span>
                    </div>
                </div>
            </CardContent>
        </Card>
    );
}
