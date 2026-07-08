"use client"

import * as React from "react"
import { SearchBar } from "@/components/SearchBar"
import { SearchResults } from "@/components/SearchResults"
import { FileManagement } from "@/components/FileManagement"
import { IndexedFiles } from "@/components/IndexedFiles"
import { TelemetryDashboard } from "@/components/TelemetryDashboard"
import { search, autocomplete, checkHealth, SearchResponse } from "@/lib/api"
import { AlertCircle, CheckCircle2 } from "lucide-react"

function useDebounce<T>(value: T, delay: number): T {
  const [debouncedValue, setDebouncedValue] = React.useState<T>(value);
  React.useEffect(() => {
    const handler = setTimeout(() => {
      setDebouncedValue(value);
    }, delay);
    return () => {
      clearTimeout(handler);
    };
  }, [value, delay]);
  return debouncedValue;
}

export default function Home() {
  const [query, setQuery] = React.useState("")
  const [results, setResults] = React.useState<SearchResponse>({ query: "", total_hits: 0, hits: [] })
  const [suggestions, setSuggestions] = React.useState<string[]>([])
  const [isSearching, setIsSearching] = React.useState(false)
  const [engineReady, setEngineReady] = React.useState<boolean | null>(null)

  const debouncedQuery = useDebounce(query, 300)

  // Check Engine Health
  React.useEffect(() => {
    checkHealth().then(setEngineReady);
  }, []);

  // Effect for Autocomplete
  React.useEffect(() => {
    async function fetchSuggestions() {
      if (debouncedQuery.length >= 1) {
        const suggs = await autocomplete(debouncedQuery)
        setSuggestions(suggs)
      } else {
        setSuggestions([])
      }
    }
    fetchSuggestions()
  }, [debouncedQuery])

  React.useEffect(() => {
    async function fetchResults() {
      if (debouncedQuery.length >= 2) {
        setIsSearching(true)
        try {
          const res = await search(debouncedQuery)
          setResults(res)
        } catch (e) {
          console.error(e)
        } finally {
          setIsSearching(false)
        }
      } else if (debouncedQuery.length === 0) {
        setResults({ query: "", total_hits: 0, hits: [] })
      }
    }
    fetchResults()
  }, [debouncedQuery])

  const handleManualSearch = async (q: string) => {
    setQuery(q)
    setIsSearching(true)
    const res = await search(q)
    setResults(res)
    setIsSearching(false)
  }

  const handleQueryChange = (q: string) => {
    setQuery(q)
  }

  return (
    <main className="flex min-h-screen flex-col items-center py-12 px-4 sm:px-8 max-w-7xl mx-auto">
      {/* Engine Status Indicator */}
      <div className="absolute top-4 right-4 flex items-center gap-2 text-sm z-50">
        {engineReady === null ? (
          <span className="text-muted-foreground">Checking engine...</span>
        ) : engineReady ? (
          <span className="text-green-600 flex items-center gap-1 bg-green-50 px-2 py-1 rounded-full"><CheckCircle2 className="h-3 w-3" /> Engine Ready</span>
        ) : (
          <span className="text-red-500 flex items-center gap-1 bg-red-50 px-2 py-1 rounded-full"><AlertCircle className="h-3 w-3" /> Engine Offline</span>
        )}
      </div>

      <div className="z-10 w-full grid grid-cols-1 lg:grid-cols-[1fr_300px] gap-8 items-start">

        <div className="flex flex-col gap-8">
          {/* Header Section */}
          <div className="text-center lg:text-left space-y-4 pt-12">
            <h1 className="text-4xl sm:text-6xl font-extrabold tracking-tight pb-2 bg-clip-text text-transparent bg-gradient-to-r from-primary to-purple-600">
              Mini Search Engine
            </h1>
            <p className="text-muted-foreground max-w-[600px] text-lg mx-auto lg:mx-0">
              Fast, secure, and purely local. Index your documents and search with speed.
            </p>
            <div className="pt-2">
              <a href="/simulation" className="inline-flex items-center gap-2 px-4 py-2 bg-indigo-600/10 text-indigo-600 hover:bg-indigo-600/20 hover:text-indigo-700 font-semibold rounded-md transition-colors border border-indigo-600/20">
                <svg className="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M14.752 11.168l-3.197-2.132A1 1 0 0010 9.87v4.263a1 1 0 001.555.832l3.197-2.132a1 1 0 000-1.664z" /><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M21 12a9 9 0 11-18 0 9 9 0 0118 0z" /></svg>
                Launch Live FM-Index Simulation
              </a>
            </div>
          </div>

          {/* Search Section */}
          <div className="w-full relative z-20">
            <SearchBar
              onSearch={handleManualSearch}
              onQueryChange={handleQueryChange}
              suggestions={suggestions}
              isSearching={isSearching}
              className="shadow-xl"
            />
          </div>

          <TelemetryDashboard />

          {/* Results Section */}
          <div className="w-full animate-in fade-in slide-in-from-bottom-4 duration-500">
            <SearchResults results={results} query={query} />
          </div>
        </div>

        {/* Sidebar: File Management & Indexing Stats */}
        <div className="flex flex-col gap-6 lg:pt-12">
          <FileManagement />
          <IndexedFiles />
        </div>

      </div>

      {/* Background decoration */}
      <div className="fixed inset-0 -z-10 h-full w-full bg-background bg-[linear-gradient(to_right,#8080800a_1px,transparent_1px),linear-gradient(to_bottom,#8080800a_1px,transparent_1px)] bg-[size:14px_24px]"></div>
      <div className="fixed left-0 right-0 top-0 -z-10 m-auto h-[310px] w-[310px] rounded-full bg-primary/20 opacity-20 blur-[100px]"></div>
    </main>
  )
}
