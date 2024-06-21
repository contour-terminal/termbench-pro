using Pkg
Pkg.add("CairoMakie")
Pkg.add("JSON")
Pkg.instantiate()
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
        name = "chars_per_line"
    elseif data_type == :sgr
        name = "chars_with_sgr_per_line"
    elseif data_type == :sgr_bg
        name = "chars_with_sgr_and_bg_per_line"
    elseif data_type == :unicode
        name = "unicode_simple"
    elseif data_type == :diacritic
        name = "unicode_diacritic"
    elseif data_type == :diacritic_double
        name = "unicode_double_diacritic"
    elseif data_type == :fire
        name = "unicode_fire"
    elseif data_type == :vt_movement
        name = "vt_movement"
    elseif data_type == :vt_insert
        name = "vt_insert"
    elseif data_type == :flag
        name = "unicode_flag"
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

    markers = [:circle :rect :cross :star4 :star5 :star6 :diamond]

    get_values_l = (type) -> get_values(data,type)
    speed = [ get_values_l(t) for t in types]

    marker_size = 8
    for (ind,dat) in enumerate(speed)
        scatter!(ax, dat,label= terminal_name * "_" * string(types[ind]), marker = markers[ind], markersize = marker_size)
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
types = [:unicode :fire :flag :diacritic :diacritic_double]
generate_for_terminal_l = (n) -> generate_for_terminal(n, "results_unicode_")
generate_for_terminal_l("contour_results")
generate_for_terminal_l("alacritty_results")
generate_for_terminal_l("xterm_results")
generate_for_terminal_l("kitty_results")
generate_for_terminal_l("wezterm_results")
types = [:vt_movement :vt_insert]
generate_for_terminal_l = (n) -> generate_for_terminal(n, "results_vt_")
generate_for_terminal_l("contour_results")
generate_for_terminal_l("alacritty_results")
generate_for_terminal_l("xterm_results")
generate_for_terminal_l("kitty_results")
generate_for_terminal_l("wezterm_results")


types =   [:ascii :sgr :sgr_bg :unicode :fire :flag :diacritic :diacritic_double :vt_movement :vt_insert]
[ save("comparison_"*string(type)*".png", generate_comparison(type)) for type in types ]
