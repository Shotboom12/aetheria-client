// Arithmetic excerpt: supplied Waystone FFT adapter, exact origin hash in review receipt.
    struct Random {
        uint32_t state;
        float Uniform() {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (static_cast<float>(state >> 8) + 0.5f) * (1.0f / 16777216.0f);
        }
        float Normal() {
            float u = Uniform();
            float v = Uniform();
            return static_cast<float>(std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * kPi * v));
        }
    };

    // Mitsuyasu spreading cos^2s(theta / 2), normalised over -pi..pi
    double Spreading(double theta, double s) {
        if (s < 0.1) s = 0.1;
        double q = std::pow(2.0, 2.0 * s - 1.0) / kPi * std::exp(2.0 * std::lgamma(s + 1.0) - std::lgamma(2.0 * s + 1.0));
        return q * std::pow(std::fabs(std::cos(theta * 0.5)), 2.0 * s);
    }

    // JONSWAP (fetch in km > 0) or Pierson-Moskowitz (fetch 0: fully developed), energy per unit angular frequency
    double Spectrum(double w, double wind, double fetchKm, double gamma) {
        if (w <= 1e-4) return 0.0;
        double alpha;
        double wp;
        double peak = 1.0;
        if (fetchKm > 0.0) {
            double fetch = fetchKm * 1000.0;
            alpha = 0.076 * std::pow(wind * wind / (fetch * kG), 0.22);
            wp = 22.0 * std::pow(kG * kG / (wind * fetch), 1.0 / 3.0);
            double sigma = w <= wp ? 0.07 : 0.09;
            double r = std::exp(-(w - wp) * (w - wp) / (2.0 * sigma * sigma * wp * wp));
            peak = std::pow(gamma > 1.0 ? gamma : 1.0, r);
        } else {
            alpha = 0.0081;
            wp = 0.855 * kG / wind;
        }
        double x = wp / w;
        return alpha * kG * kG / std::pow(w, 5.0) * std::exp(-1.25 * x * x * x * x) * peak;
    }

    void BuildCascade(Cascade& c, const Tile& t, double kLow, double kHigh, double heading) {
        c.size = t.size;
        c.h0.assign(kN * kN, Complex());
        c.omega.assign(kN * kN, 0.0f);
        Random rnd;
        rnd.state = static_cast<uint32_t>(t.seed) * 2654435761u + 0x9E3779B9u;
        if (!rnd.state) rnd.state = 1;
        double dk = 2.0 * kPi / t.size;
        double wind = t.wind > 0.5 ? t.wind : 0.5;
        for (int32_t m = 0; m < kN; m++) {
            for (int32_t n = 0; n < kN; n++) {
                // the random numbers are drawn for every k (the same field whatever the band)
                float xr = rnd.Normal();
                float xi = rnd.Normal();
                double kx = (n - kN / 2) * dk;
                double ky = (m - kN / 2) * dk;
                double k = std::sqrt(kx * kx + ky * ky);
                int32_t i = m * kN + n;
                c.omega[i] = static_cast<float>(std::sqrt(kG * k));
                if (k < 1e-6 || k < kLow || k >= kHigh) continue;
                double w = std::sqrt(kG * k);
                double dwdk = kG / (2.0 * w);
                double theta = std::atan2(ky, kx);
                double s = 0.0;
                int32_t used = 0;
                for (int32_t g = 0; g < 2; g++) {
                    if (t.group[g][1] <= 0.0f) continue;
                    s += Spectrum(w, wind, t.group[g][1], t.gamma) * Spreading(theta - heading - t.group[g][0], t.group[g][2]);
                    used++;
                }
                if (!used) s = Spectrum(w, wind, 0.0, 1.0) * Spreading(theta - heading, t.group[0][2] > 0.0f ? t.group[0][2] : 1.0f);
                // S(kx, ky) = S(w) dw/dk D(theta) / k. h(k, t) gets h0(k) and conj(h0(-k)), so E|h0|^2 = S dk^2 / 2
                // keeps the height variance = the sum of S dk^2 over the grid (checked numerically);
                // split over the two random numbers
                double p = 0.5 * s * dwdk / k * dk * dk;
                double a = std::sqrt(p * 0.5);
                c.h0[i].re = static_cast<float>(xr * a);
                c.h0[i].im = static_cast<float>(xi * a);
            }
        }
    }

    // in-place radix-2 inverse FFT (no scaling) of kN complex values with stride
    void Fft1D(Complex* data, int32_t stride) {
        for (int32_t i = 1, j = 0; i < kN; i++) {
            int32_t bit = kN >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(data[i * stride], data[j * stride]);
        }
        for (int32_t len = 2; len <= kN; len <<= 1) {
            double a = 2.0 * kPi / len;
            float wr = static_cast<float>(std::cos(a));
            float wi = static_cast<float>(std::sin(a));
            for (int32_t i = 0; i < kN; i += len) {
                float cr = 1.0f;
                float ci = 0.0f;
                for (int32_t j = 0; j < len / 2; j++) {
                    Complex& u = data[(i + j) * stride];
                    Complex& v = data[(i + j + len / 2) * stride];
                    float tr = v.re * cr - v.im * ci;
                    float ti = v.re * ci + v.im * cr;
                    v.re = u.re - tr;
                    v.im = u.im - ti;
                    u.re += tr;
                    u.im += ti;
                    float nr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr;
                    cr = nr;
                }
            }
        }
    }

    void Fft2D(std::vector<Complex>& f) {
        for (int32_t m = 0; m < kN; m++) Fft1D(&f[m * kN], 1);
        for (int32_t n = 0; n < kN; n++) Fft1D(&f[n], kN);
    }

    void Evaluate(const Cascade& c, double t, float scale, std::vector<float>& out) {
        std::vector<Complex> a(kN * kN), b(kN * kN), d(kN * kN);
        double dk = 2.0 * kPi / c.size;
        for (int32_t m = 0; m < kN; m++) {
            for (int32_t n = 0; n < kN; n++) {
                int32_t i = m * kN + n;
                // h(k, t) = h0(k) e^(iwt) + conj(h0(-k)) e^(-iwt)
                int32_t mm = (kN - m) % kN;
                int32_t nn = (kN - n) % kN;
                const Complex& p = c.h0[i];
                const Complex& q = c.h0[mm * kN + nn];
                double ph = std::fmod(c.omega[i] * t, 2.0 * kPi);
                float cw = static_cast<float>(std::cos(ph));
                float sw = static_cast<float>(std::sin(ph));
                float hr = (p.re * cw - p.im * sw) + (q.re * cw - q.im * sw);
                float hi = (p.re * sw + p.im * cw) - (q.re * sw + q.im * cw);
                hr *= scale;
                hi *= scale;
                float kx = static_cast<float>((n - kN / 2) * dk);
                float ky = static_cast<float>((m - kN / 2) * dk);
                float k = std::sqrt(kx * kx + ky * ky);
                float ik = k > 1e-6f ? 1.0f / k : 0.0f;
                // the sign (-1)^(n + m) moves k = 0 from the middle of the array to its corner (a centred spectrum)
                float sg = ((n + m) & 1) ? -1.0f : 1.0f;
                // a = h + i * dh/dx, b = dh/dy + i * dDx/dx, d = dDy/dy + i * dDx/dy (packed real pairs)
                float sxr = -kx * hi, sxi = kx * hr;     // i kx h
                float syr = -ky * hi, syi = ky * hr;     // i ky h
                float xxr = kx * kx * ik * hr, xxi = kx * kx * ik * hi;
                float yyr = ky * ky * ik * hr, yyi = ky * ky * ik * hi;
                float xyr = kx * ky * ik * hr, xyi = kx * ky * ik * hi;
                a[i].re = sg * (hr - sxi);  a[i].im = sg * (hi + sxr);
                b[i].re = sg * (syr - xxi); b[i].im = sg * (syi + xxr);
                d[i].re = sg * (yyr - xyi); d[i].im = sg * (yyi + xyr);
            }
        }
        Fft2D(a);
        Fft2D(b);
        Fft2D(d);
        out.resize(kN * kN * 4);
        for (int32_t i = 0; i < kN * kN; i++) {
            // undo the centring shift on the output side as well
            int32_t m = i / kN;
            int32_t n = i % kN;
            float sg = ((n + m) & 1) ? -1.0f : 1.0f;
            float h = sg * a[i].re;
            float sx = sg * a[i].im;
            float sy = sg * b[i].re;
            float dxx = sg * b[i].im;
            float dyy = sg * d[i].re;
            float dxy = sg * d[i].im;
            // horizontal displacement x' = x - D (choppiness 1): Jacobian of that map
            float jac = (1.0f - dxx) * (1.0f - dyy) - dxy * dxy;
            out[i * 4 + 0] = sx;
            out[i * 4 + 1] = sy;
            out[i * 4 + 2] = h;
            out[i * 4 + 3] = jac;
        }
    }

