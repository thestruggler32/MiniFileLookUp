"use client";

import { useState, useMemo, useEffect } from "react";
import { ArrowLeft, Play, StepForward, RotateCcw, ChevronRight, Hash } from "lucide-react";
import Link from "next/link";
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from "@/components/ui/card";

// Helpers for the algorithms
const buildSuffixArray = (text: string) => {
  const suffixes = [];
  for (let i = 0; i < text.length; i++) {
    suffixes.push({ index: i, suffix: text.slice(i) });
  }
  suffixes.sort((a, b) => a.suffix.localeCompare(b.suffix));
  return suffixes;
};

const buildBWT = (text: string, sa: { index: number; suffix: string }[]) => {
  const L = [];
  const F = [];
  for (let i = 0; i < sa.length; i++) {
    const idx = sa[i].index;
    const prevChar = idx === 0 ? text[text.length - 1] : text[idx - 1];
    L.push(prevChar);
    F.push(sa[i].suffix[0]);
  }
  return { F, L };
};

const calculateRanks = (col: string[]) => {
  const ranks: number[] = [];
  const counts: Record<string, number> = {};
  for (let i = 0; i < col.length; i++) {
    const char = col[i];
    counts[char] = (counts[char] || 0) + 1;
    ranks.push(counts[char]);
  }
  return ranks;
};

// Calculates the mapping from L to F (where does L[i] appear in F?)
const buildLFMapping = (F: string[], L: string[], F_ranks: number[], L_ranks: number[]) => {
  const mapping: (number | null)[] = [];
  for (let i = 0; i < L.length; i++) {
    const char = L[i];
    const rank = L_ranks[i];
    
    // Find this char with this rank in F
    let fIndex = null;
    for (let j = 0; j < F.length; j++) {
      if (F[j] === char && F_ranks[j] === rank) {
        fIndex = j;
        break;
      }
    }
    mapping.push(fIndex);
  }
  return mapping;
};

export default function SimulationPage() {
  const [corpus, setCorpus] = useState("cache memory$");
  const [query, setQuery] = useState("mem");
  const [step, setStep] = useState(0);

  // Auto-append $ if missing
  const safeCorpus = corpus.endsWith("$") ? corpus : corpus + "$";
  
  // Memoized Algorithm Data
  const { sa, F, L, F_ranks, L_ranks, lf_mapping, searchSteps } = useMemo(() => {
    const sa = buildSuffixArray(safeCorpus);
    const { F, L } = buildBWT(safeCorpus, sa);
    const F_ranks = calculateRanks(F);
    const L_ranks = calculateRanks(L);
    const lf_mapping = buildLFMapping(F, L, F_ranks, L_ranks);

    // Simulate search steps
    const steps = [];
    if (query.length > 0) {
      let charIdx = query.length - 1;
      let currentChar = query[charIdx];
      
      // Step 0: Initial find in F
      let rangeStart = F.indexOf(currentChar);
      let rangeEnd = F.lastIndexOf(currentChar);
      
      if (rangeStart !== -1) {
        steps.push({
          desc: `Start backward search. Looking for last character '${currentChar}' in F column.`,
          activeF: Array.from({length: rangeEnd - rangeStart + 1}, (_, i) => rangeStart + i),
          activeL: []
        });

        // LF Mapping steps
        while (charIdx > 0 && rangeStart !== -1 && rangeStart <= rangeEnd) {
          charIdx--;
          const nextChar = query[charIdx];
          
          steps.push({
            desc: `Look at the L column for active rows. We need to find '${nextChar}' preceding our current matches.`,
            activeF: Array.from({length: rangeEnd - rangeStart + 1}, (_, i) => rangeStart + i),
            activeL: Array.from({length: rangeEnd - rangeStart + 1}, (_, i) => rangeStart + i)
          });
          
          let nextStart = -1;
          let nextEnd = -1;
          
          // Find occurrences of nextChar in L within [rangeStart, rangeEnd]
          const matchingLIndices = [];
          for(let i = rangeStart; i <= rangeEnd; i++) {
              if (L[i] === nextChar) matchingLIndices.push(i);
          }
          
          if (matchingLIndices.length === 0) {
              steps.push({
                  desc: `Character '${nextChar}' not found in active L column rows. Query does not exist!`,
                  activeF: [], activeL: []
              });
              rangeStart = -1;
              break;
          }
          
          steps.push({
             desc: `Found '${nextChar}' in L column. Using Rank to jump back to the F column (LF-Mapping).`,
             activeF: matchingLIndices.map(idx => lf_mapping[idx] as number),
             activeL: matchingLIndices
          });
          
          nextStart = lf_mapping[matchingLIndices[0]] as number;
          nextEnd = lf_mapping[matchingLIndices[matchingLIndices.length-1]] as number;
          
          rangeStart = nextStart;
          rangeEnd = nextEnd;
          
          steps.push({
            desc: `Mapped to new range in F column. Range narrowed!`,
            activeF: Array.from({length: rangeEnd - rangeStart + 1}, (_, i) => rangeStart + i),
            activeL: []
          });
        }
        
        if (rangeStart !== -1) {
            steps.push({
                desc: `Search complete! Found ${rangeEnd - rangeStart + 1} matches. We map the F indices back to the Suffix Array to get original offsets.`,
                activeF: Array.from({length: rangeEnd - rangeStart + 1}, (_, i) => rangeStart + i),
                activeL: [],
                success: true
            });
        }
      } else {
        steps.push({
            desc: `Character '${currentChar}' not found in F column. Query does not exist!`,
            activeF: [], activeL: []
        });
      }
    }

    return { sa, F, L, F_ranks, L_ranks, lf_mapping, searchSteps: steps };
  }, [safeCorpus, query]);

  const currentStep = searchSteps[step] || { desc: "Ready to search.", activeF: [], activeL: [] };

  return (
    <div className="min-h-screen bg-background text-foreground p-6">
      <div className="max-w-7xl mx-auto space-y-6">
        
        {/* HEADER */}
        <div className="flex items-center gap-4 border-b pb-4">
          <Link href="/" className="p-2 bg-muted rounded-md hover:bg-muted/80 transition-colors">
            <ArrowLeft className="h-5 w-5" />
          </Link>
          <div>
            <h1 className="text-3xl font-bold bg-gradient-to-r from-blue-500 to-indigo-600 bg-clip-text text-transparent">
              FM-Index Interactive Visualizer
            </h1>
            <p className="text-muted-foreground">Watch the compression and backward search algorithm execute in real-time.</p>
          </div>
        </div>

        <div className="grid grid-cols-1 lg:grid-cols-12 gap-6">
          
          {/* LEFT PANEL - CONTROLS & SA */}
          <div className="lg:col-span-4 space-y-6">
            <Card className="border-primary/20 shadow-lg">
              <CardHeader className="bg-muted/30 pb-4">
                <CardTitle className="text-lg">Simulation Inputs</CardTitle>
                <div className="text-sm text-muted-foreground mt-1 text-red-400">Keep strings short (10-20 chars) for UI legibility.</div>
              </CardHeader>
              <CardContent className="space-y-4 pt-4">
                <div>
                  <label className="text-sm font-medium mb-1 block">Corpus String (Auto-appends $)</label>
                  <input 
                    type="text" 
                    value={corpus} 
                    onChange={(e) => { setCorpus(e.target.value.toLowerCase()); setStep(0); }}
                    className="w-full p-2 bg-background border rounded-md font-mono"
                    maxLength={30}
                  />
                </div>
                <div>
                  <label className="text-sm font-medium mb-1 block">Search Query</label>
                  <input 
                    type="text" 
                    value={query} 
                    onChange={(e) => { setQuery(e.target.value.toLowerCase()); setStep(0); }}
                    className="w-full p-2 bg-background border rounded-md font-mono"
                  />
                </div>
              </CardContent>
            </Card>

            <Card>
              <CardHeader className="pb-3 border-b border-border/50 bg-muted/10">
                <CardTitle className="text-base flex items-center gap-2">
                  <Hash className="h-4 w-4" /> Suffix Array (SA)
                </CardTitle>
              </CardHeader>
              <CardContent className="p-0">
                <div className="max-h-[400px] overflow-y-auto">
                  <table className="w-full text-sm font-mono text-left">
                    <thead className="bg-muted/50 sticky top-0">
                      <tr>
                        <th className="p-2 border-b">Idx</th>
                        <th className="p-2 border-b">SA</th>
                        <th className="p-2 border-b">Sorted Suffixes</th>
                      </tr>
                    </thead>
                    <tbody>
                      {sa.map((item, i) => (
                        <tr key={i} className={`border-b border-border/20 ${currentStep.activeF.includes(i) ? 'bg-primary/20' : 'hover:bg-muted/30'}`}>
                          <td className="p-2 text-muted-foreground">{i}</td>
                          <td className="p-2 text-blue-500 font-bold">{item.index}</td>
                          <td className="p-2 truncate max-w-[150px]">{item.suffix}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </CardContent>
            </Card>
          </div>

          {/* RIGHT PANEL - BWT & LF MAPPING */}
          <div className="lg:col-span-8 space-y-6">
            
            {/* STEPPER CONTROLS */}
            <Card className="border-indigo-500/30 shadow-lg relative overflow-hidden">
                <div className="absolute inset-0 bg-gradient-to-r from-indigo-500/10 to-blue-500/10 z-0"></div>
                <CardContent className="p-6 relative z-10">
                    <div className="flex flex-col sm:flex-row sm:items-center gap-4 justify-between">
                        <div className="space-y-1 flex-1">
                            <h3 className="font-semibold text-lg flex items-center gap-2">
                                <Play className="h-5 w-5 text-indigo-500" /> 
                                Step {step + 1} of {Math.max(1, searchSteps.length)}
                            </h3>
                            <p className="text-muted-foreground font-mono bg-background/50 p-2 rounded border inline-block">
                                {currentStep.desc}
                            </p>
                            {currentStep.success && (
                                <div className="mt-2 text-emerald-500 font-bold flex items-center gap-2">
                                    ✓ Match Found at SA offsets: {currentStep.activeF.map(i => sa[i].index).join(", ")}
                                </div>
                            )}
                        </div>
                        <div className="flex gap-2 shrink-0">
                            <button 
                                onClick={() => setStep(0)}
                                className="p-2 border rounded hover:bg-muted flex items-center gap-1"
                                disabled={step === 0}
                            >
                                <RotateCcw className="h-4 w-4" /> Reset
                            </button>
                            <button 
                                onClick={() => setStep(s => Math.min(searchSteps.length - 1, s + 1))}
                                className="px-4 py-2 bg-indigo-600 hover:bg-indigo-700 text-white rounded font-medium shadow flex items-center gap-1"
                                disabled={step >= searchSteps.length - 1 || searchSteps.length === 0}
                            >
                                Next Step <StepForward className="h-4 w-4" />
                            </button>
                        </div>
                    </div>
                </CardContent>
            </Card>

            {/* BWT VISUALIZER TABLE */}
            <Card>
                <CardHeader className="pb-3 border-b border-border/50 bg-muted/10">
                    <CardTitle className="text-base flex items-center justify-between">
                        <span>BWT & LF-Mapping Engine</span>
                        <div className="flex gap-4 text-xs font-mono">
                            <span className="flex items-center gap-1"><div className="w-3 h-3 bg-indigo-500/30 border border-indigo-500"></div> F-Col Focus</span>
                            <span className="flex items-center gap-1"><div className="w-3 h-3 bg-rose-500/30 border border-rose-500"></div> L-Col Focus</span>
                        </div>
                    </CardTitle>
                </CardHeader>
                <CardContent className="p-0">
                    <div className="overflow-x-auto">
                        <table className="w-full text-center font-mono">
                            <thead className="bg-muted/50 border-b">
                                <tr>
                                    <th className="p-3 text-muted-foreground">Index</th>
                                    <th className="p-3 text-indigo-400">First (F)</th>
                                    <th className="p-3 text-xs text-muted-foreground">F-Rank</th>
                                    <th className="p-3 text-muted-foreground w-full">... intervening string ...</th>
                                    <th className="p-3 text-xs text-muted-foreground">L-Rank</th>
                                    <th className="p-3 text-rose-400">Last (L) / BWT</th>
                                </tr>
                            </thead>
                            <tbody>
                                {sa.map((_, i) => {
                                    const isFActive = currentStep.activeF.includes(i);
                                    const isLActive = currentStep.activeL.includes(i);
                                    
                                    return (
                                        <tr key={i} className="border-b border-border/10">
                                            <td className="p-2 text-muted-foreground border-r">{i}</td>
                                            
                                            {/* F Column */}
                                            <td className={`p-2 font-bold text-lg transition-all duration-300
                                                ${isFActive ? 'bg-indigo-500/30 text-indigo-300 shadow-[inset_0_0_10px_rgba(99,102,241,0.5)]' : ''}`}>
                                                {F[i]}
                                            </td>
                                            <td className={`p-2 text-xs ${isFActive ? 'text-indigo-400' : 'text-muted-foreground/50'}`}>
                                                {F[i]}_{F_ranks[i]}
                                            </td>
                                            
                                            <td className="p-2 text-muted-foreground/30 text-xs tracking-[0.2em]">
                                                {isFActive && isLActive ? '← MAP ←' : (isFActive || isLActive ? '........' : '')}
                                            </td>
                                            
                                            {/* L Column (BWT) */}
                                            <td className={`p-2 text-xs ${isLActive ? 'text-rose-400' : 'text-muted-foreground/50'}`}>
                                                {L[i]}_{L_ranks[i]}
                                            </td>
                                            <td className={`p-2 font-bold text-lg border-l transition-all duration-300
                                                ${isLActive ? 'bg-rose-500/30 text-rose-300 shadow-[inset_0_0_10px_rgba(244,63,94,0.5)]' : ''}`}>
                                                {L[i]}
                                            </td>
                                        </tr>
                                    );
                                })}
                            </tbody>
                        </table>
                    </div>
                    
                    <div className="p-4 bg-muted/20 border-t flex flex-col gap-2">
                        <div className="text-sm font-semibold mb-1">Final Compressed Outputs (What C-Engine Actually Stores):</div>
                        <div className="flex items-center gap-2">
                            <span className="text-xs text-muted-foreground w-24">BWT String:</span>
                            <code className="bg-background px-2 py-1 rounded text-rose-400 tracking-wider font-bold shadow-inner">
                                {L.join('')}
                            </code>
                        </div>
                        <div className="flex items-center gap-2">
                            <span className="text-xs text-muted-foreground w-24">Wavelet Tree:</span>
                            <span className="text-xs text-muted-foreground italic">(Implicitly encodes the BWT string above for O(1) Rank operations)</span>
                        </div>
                    </div>
                </CardContent>
            </Card>

          </div>
        </div>
      </div>
    </div>
  );
}
