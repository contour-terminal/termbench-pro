using Pkg
Pkg.add("CairoMakie")
using CairoMakie

function parse_line(line)
   if count("MB",line) > 0
       return parse(Float64,strip(split(split(line,',')[2],"MB/s")[1]))
   else # KB
       return parse(Float64,split(split(split(line,",")[2], "(")[1]," ")[2]) / 1024
   end
end

function get_data(data, data_type)
    if data_type == :ascii
        name = "chars per line"
    elseif data_type == :sgr
        name = "chars with sgr per line"
    elseif data_type == :sgr_bg
        name = "chars with sgr and bg per line"
    end

    lines = []
    for el in data
        if occursin(name, el)
            push!(lines, el)
        end
    end
    return [ parse_line(val) for val in lines]
end

function insert_from_data(ax, file_name, marker_style, type)
    data = split(open(io->read(io, String), file_name), '\n')
    terminal_name = split(file_name,"_")[1]

    data_speed = get_data(data,type)
    scatter!(ax, data_speed, label= terminal_name * "_ascii", marker = marker_style, markersize = 8)
end



function generate_for_terminal(file_name)
    terminal_name = split(file_name,"_")[1]

    fig = Figure()
    ax = Axis(fig[1,1], title = "Results for "*terminal_name ,xlabel = "Length of line", ylabel = "throughput, MB/s")

    data = split(open(io->read(io, String), file_name), '\n')
    terminal_name = split(file_name,"_")[1]

    get_data_l = (type) -> get_data(data,type)
    ascii_speed = get_data_l(:ascii)
    sgr_speed = get_data_l(:sgr)
    sgr_bg_speed = get_data_l(:sgr_bg)

    marker_size = 8
    scatter!(ax,ascii_speed, label= terminal_name * "_ascii", marker = :circle, markersize = marker_size)
    scatter!(ax,sgr_speed, label=terminal_name*"_sgr", marker = :rect, markersize = marker_size)
    scatter!(ax,sgr_bg_speed, label=terminal_name*"_sgr_and_bg", marker = :cross, markersize = marker_size)
    axislegend(position = :rt)

    save("results_"*terminal_name*".png", fig)
end

function generate_comparison(type)
    fig = Figure()
    ax = Axis(fig[1,1], title = "Comparison for "*string(type), xlabel = "Length of line", ylabel = "throughput, MB/s")

    insert_from_data(ax,"contour_results",:circle, type)
    insert_from_data(ax,"alacritty_results",:rect, type)
    insert_from_data(ax,"xterm_results",:cross, type)
    insert_from_data(ax,"kitty_results",:utriangle, type)
    axislegend(position = :lt)
    return fig
end


generate_for_terminal("contour_results")
generate_for_terminal("alacritty_results")
generate_for_terminal("xterm_results")
generate_for_terminal("kitty_results")


save("comparison_ascii.png", generate_comparison(:ascii))
save("comparison_sgr.png", generate_comparison(:sgr))
save("comparison_sgr_bg.png", generate_comparison(:sgr_bg))
