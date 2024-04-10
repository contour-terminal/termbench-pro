using Pkg
Pkg.add("CairoMakie")
Pkg.add("JSON")
using CairoMakie
using JSON



function parse_line(line)
   if count("MB",line) > 0
       return parse(Float64,strip(split(split(line,',')[2],"MB/s")[1]))
   else # KB
       return parse(Float64,split(split(split(line,",")[2], "(")[1]," ")[2]) / 1024
   end
end

function get_data(file_name)
    return JSON.parsefile(file_name)
end

function get_values(data, data_type)
    if data_type == :ascii
        name = "chars per line"
    elseif data_type == :sgr
        name = "chars with sgr per line"
    elseif data_type == :sgr_bg
        name = "chars with sgr and bg per line"
    elseif data_type == :unicode
        name = "unicode simple"
    elseif data_type == :diacritic
        name = "unicode diacritic"
    elseif data_type == :diacritic_double
        name = "unicode double diacritic"
    elseif data_type == :fire
        name = "unicode fire"
    elseif data_type == :fire_text
        name = "unicode fire as text"
    elseif data_type == :flag
        name = "unicode flag"
    end

    lines = Vector{Float64}()
    for el in data
        if occursin(name, el["name"])
            push!(lines, el["MB/s"])
        end
    end
    return lines
end

function insert_from_data(ax, file_name, marker_style, type)
    data = get_data(file_name)
    terminal_name = split(file_name,"_")[1]

    data_speed = get_values(data,type)
    scatter!(ax, data_speed, label= terminal_name * "_ascii", marker = marker_style, markersize = 8)
end



function generate_for_terminal(file_name, prefix="results_")
    try
    terminal_name = split(file_name,"_")[1]

    fig = Figure()
    ax = Axis(fig[1,1], title = "Results for "*terminal_name ,xlabel = "Length of line", ylabel = "throughput, MB/s")

    data = get_data(file_name)

    markers = [:circle :rect :cross :star4 :start5 :star6 :diamond]

    get_values_l = (type) -> get_values(data,type)
    speed = [ get_values_l(t) for t in types]

    marker_size = 8
    for (ind,dat) in enumerate(speed)
        scatter!(ax,dat, label= terminal_name * "_" * string(types[ind]), marker = markers[ind], markersize = marker_size)
    end
    axislegend(position = :rt)

    save(prefix*terminal_name*".png", fig)
    catch e
        println(e)
    end
end

function generate_comparison(type)
    try
    fig = Figure()
    ax = Axis(fig[1,1], title = "Comparison for "*string(type), xlabel = "Length of line", ylabel = "throughput, MB/s")

    insert_from_data(ax,"contour_results",:circle, type)
    insert_from_data(ax,"alacritty_results",:rect, type)
    insert_from_data(ax,"xterm_results",:cross, type)
    insert_from_data(ax,"kitty_results",:utriangle, type)
    insert_from_data(ax,"wezterm_results",:diamond, type)
    axislegend(position = :lt)
    return fig
    catch e
        println(e)
    end
end


types =   [:ascii  :sgr  :sgr_bg ]
generate_for_terminal("contour_results")
generate_for_terminal("alacritty_results")
generate_for_terminal("xterm_results")
generate_for_terminal("kitty_results")
generate_for_terminal("wezterm_results")
types = [:unicode :fire :flag :diacritic :diacritic_double :fire_text]
generate_for_terminal_l = (n) -> generate_for_terminal(n, "results_unicode_")
generate_for_terminal_l("contour_results")
generate_for_terminal_l("alacritty_results")
generate_for_terminal_l("xterm_results")
generate_for_terminal_l("kitty_results")
generate_for_terminal_l("wezterm_results")

types =   [:ascii :sgr :sgr_bg :unicode :fire :fire_text :flag :diacritic :diacritic_double]
[ save("comparison_"*string(type)*".png", generate_comparison(type)) for type in types ]
