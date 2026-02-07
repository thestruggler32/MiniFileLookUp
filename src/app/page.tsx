"use client"

import * as React from "react"
import { SearchBar } from "@/components/SearchBar"
import { SearchResults } from "@/components/SearchResults"
import { FileManagement } from "@/components/FileManagement"
import { search, autocomplete, SearchResult } from "@/lib/api"

// Inline debounce for simplicity if we don't want a separate file yet, 
// but separate file is cleaner. I'll implement a simple one here or distinct file.
// Let's create a hooks directory? Or just inline it. 
// Given the instructions, "Code should be readable". I'll create a hook file next or use a simple timer here.
// I'll implement the hook logic inside the component to avoid extra file steps unless needed for complexity.
// Actually, I'll create `src/hooks/use-debounce.ts` for clean code.

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
  const [results, setResults] = React.useState<SearchResult[]>([])
  const [suggestions, setSuggestions] = React.useState<string[]>([])
  const [isSearching, setIsSearching] = React.useState(false)

  // specific debounce for autocomplete queries
  const debouncedQuery = useDebounce(query, 300)

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

  // Effect for Instant Search (optional, or trigger on Enter)
  // Requirement: "Trigger search on Enter key" AND "Search button click"
  // "Support live updates as the user types" -> Suggests instant search results too?
  // I will implement instant search for results as well when query length is sufficient, to "wow" the user.
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
        setResults([])
      }
    }
    fetchResults()
  }, [debouncedQuery])

  const handleManualSearch = async (q: string) => {
    setQuery(q) // Update local if needed
    setIsSearching(true)
    const res = await search(q)
    setResults(res)
    setIsSearching(false)
  }

  const handleQueryChange = (q: string) => {
    setQuery(q)
  }

  return (
    <main className="flex min-h-screen flex-col items-center py-24 px-4 sm:px-8">
      <div className="z-10 w-full max-w-5xl items-center justify-between text-sm flex flex-col gap-8">

        {/* Header Section */}
        <div className="text-center space-y-4 mb-8">
          <h1 className="text-4xl sm:text-6xl font-extrabold tracking-tight pb-2 bg-clip-text text-transparent bg-gradient-to-r from-primary to-purple-600">
            Mini Search Engine
          </h1>
          <p className="text-muted-foreground max-w-[600px] text-lg">
            Fast, secure, and purely local. Index your documents and search with speed.
          </p>
        </div>

        {/* File Management Component */}
        <FileManagement />

        {/* Search Section */}
        <div className="w-full relative z-20">
          <SearchBar
            onSearch={handleManualSearch}
            onQueryChange={handleQueryChange}
            suggestions={suggestions}
            isSearching={isSearching}
            className="shadow-2xl"
          />
          {/* Note: SearchBar internally calls onSearch prop on type too if we wired it that way, 
              but in my previous SearchBar impl, it called onSearch on input change. 
              Wait, I implemented onSearch(value) in handleInputChange in SearchBar.tsx.
              So handleManualSearch is getting called on every keystroke. 
              That drives the `query` state here.
          */}
        </div>

        {/* Results Section */}
        <div className="w-full animate-in fade-in slide-in-from-bottom-4 duration-500">
          <SearchResults results={results} query={query} />
        </div>

      </div>

      {/* Background decoration */}
      <div className="fixed inset-0 -z-10 h-full w-full bg-background bg-[linear-gradient(to_right,#8080800a_1px,transparent_1px),linear-gradient(to_bottom,#8080800a_1px,transparent_1px)] bg-[size:14px_24px]"></div>
      <div className="fixed left-0 right-0 top-0 -z-10 m-auto h-[310px] w-[310px] rounded-full bg-primary/20 opacity-20 blur-[100px]"></div>
    </main>
  )
}
