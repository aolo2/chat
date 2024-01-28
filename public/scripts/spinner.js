// Slightly modified from
// https://stackoverflow.com/a/18473154
function spinner_polar_to_cartesian(centerX, centerY, radius, angleInDegrees) {
  const angleInRadians = (angleInDegrees - 90) * Math.PI / 180.0;

  return {
    'x': centerX + (radius * Math.cos(angleInRadians)),
    'y': centerY + (radius * Math.sin(angleInRadians))
  };
}

function spinner_describe_arc(x, y, radius, startAngle, endAngle) {
    const start = spinner_polar_to_cartesian(x, y, radius, endAngle);
    const end = spinner_polar_to_cartesian(x, y, radius, startAngle);
    
    var largeArcFlag = (Math.abs(endAngle - startAngle) % 360) <= 180 ? "0" : "1";

    var d = [
        'M', start.x, start.y, 
        'A', radius, radius, 0, largeArcFlag, 0, end.x, end.y
    ].join(' ');

    return d;
}

function spinner_init(spinner) {
    let a_angel = 0;
    let b_angel = 180;
    let speed_a = 2;
    let speed_b = 6;
    let raf = null;

    const step_spinner = () => {
        const str = spinner_describe_arc(32, 32, 16, a_angel, b_angel);

        a_angel += speed_a;
        b_angel += speed_b;

        const diff = b_angel - a_angel;

        if (diff > 320) {
            speed_a = 6;
            speed_b = 2;
        } else if (diff < 40) {
            speed_a = 2;
            speed_b = 6;
        }

        spinner.setAttribute('d', str);

        raf = window.requestAnimationFrame(() => step_spinner());
    }

    const stop_spinner = () => {
        if (raf !== null) {
            window.cancelAnimationFrame(raf);
        }
    }

    return {
        'start': step_spinner,
        'stop': stop_spinner
    };
}